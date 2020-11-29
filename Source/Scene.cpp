#include "stdafx.h"
#include "Scene.h"
#include "D3D11RenderSystem.h"
#include "CommandLineArgs.h"
#include "Mesh.h"
#include "GPUTexture.h"
#include "GPUBuffer.h"
#include "Shader.h"
#include "imgui/imgui.h"
#include "../Shaders/SumLuminanceDef.inc.hlsl"

using namespace DirectX;

Scene::Scene()
    : m_IsFilmDirty( true )
    , m_UniformRealDistribution( 0.0f, std::nexttoward( 1.0f, 0.0f ) )
    , m_PointLightSelectionIndex( -1 )
    , m_IsConstantBufferDirty( true )
    , m_IsPointLightBufferDirty( false )
{
    std::random_device randomDevice;
    m_MersenneURBG = std::mt19937( randomDevice() );
}

Scene::~Scene()
{
}

bool Scene::Init()
{
    uint32_t resolutionWidth = CommandLineArgs::Singleton()->ResolutionX();
    uint32_t resolutionHeight = CommandLineArgs::Singleton()->ResolutionY();

    ID3D11Device* device = GetDevice();

    m_FilmTexture.reset( GPUTexture::Create(
        resolutionWidth
        , resolutionHeight
        , DXGI_FORMAT_R32G32B32A32_FLOAT
        , GPUResourceCreationFlags_HasUAV | GPUResourceCreationFlags_IsRenderTarget ) );
    if ( !m_FilmTexture )
        return false;

    m_CookTorranceCompETexture.reset( GPUTexture::CreateFromFile( L"BuiltinResources\\CookTorranceComp_E.DDS" ) );
    if ( !m_CookTorranceCompETexture )
        return false;

    m_CookTorranceCompEAvgTexture.reset( GPUTexture::CreateFromFile( L"BuiltinResources\\CookTorranceComp_E_Avg.DDS" ) );
    if ( !m_CookTorranceCompEAvgTexture )
        return false;

    m_CookTorranceCompInvCDFTexture.reset( GPUTexture::CreateFromFile( L"BuiltinResources\\CookTorranceComp_InvCDF.DDS" ) );
    if ( !m_CookTorranceCompInvCDFTexture )
        return false;

    m_CookTorranceCompPdfScaleTexture.reset( GPUTexture::CreateFromFile( L"BuiltinResources\\CookTorranceComp_PdfScale.DDS" ) );
    if ( !m_CookTorranceCompPdfScaleTexture )
        return false;

    m_CookTorranceCompEFresnelTexture.reset( GPUTexture::CreateFromFile( L"BuiltinResources\\CookTorranceComp_EFresnel.DDS" ) );
    if ( !m_CookTorranceCompEFresnelTexture )
        return false;

    m_EnvironmentTexture.reset( GPUTexture::CreateFromFile( CommandLineArgs::Singleton()->GetEnvironmentTextureFilename().c_str() ) );
    if ( !m_EnvironmentTexture )
        return false;

    std::vector<D3D_SHADER_MACRO> rayTracingShaderDefines;
    if ( CommandLineArgs::Singleton()->GetNoBVHAccel() )
    {
        rayTracingShaderDefines.push_back( { "NO_BVH_ACCEL", "0" } );
    }
    rayTracingShaderDefines.push_back( { NULL, NULL } );
    m_RayTracingShader.reset( ComputeShader::CreateFromFile( L"Shaders\\RayTracing.hlsl", rayTracingShaderDefines ) );
    if ( !m_RayTracingShader )
        return false;

    m_RayTracingConstantsBuffer.reset( GPUBuffer::Create(
          sizeof( RayTracingConstants )
        , 0
        , GPUResourceCreationFlags_CPUWriteable | GPUResourceCreationFlags_IsConstantBuffer ) );
    if ( !m_RayTracingConstantsBuffer )
        return false;

    m_SamplesBuffer.reset( GPUBuffer::Create(
          sizeof( float ) * kMaxSamplesCount
        , sizeof( float )
        , GPUResourceCreationFlags_CPUWriteable | GPUResourceCreationFlags_IsStructureBuffer ) );
    if ( !m_SamplesBuffer )
        return false;

    m_SampleCounterBuffer.reset( GPUBuffer::Create(
          4
        , 4
        , GPUResourceCreationFlags_IsStructureBuffer | GPUResourceCreationFlags_HasUAV ) );
    if ( !m_SampleCounterBuffer )
        return false;

    m_DefaultRenderTarget.reset( GPUTexture::CreateFromSwapChain() );
    if ( !m_DefaultRenderTarget )
        return false;

    D3D11_SAMPLER_DESC samplerDesc;
    ZeroMemory( &samplerDesc, sizeof( D3D11_SAMPLER_DESC ) );
    samplerDesc.Filter = D3D11_FILTER_MAXIMUM_MIN_MAG_MIP_POINT;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MaxAnisotropy = 1;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    HRESULT hr = device->CreateSamplerState( &samplerDesc, &m_UVClampSamplerState );
    if ( FAILED( hr ) )
        return false;

    m_RayTracingConstants.samplesCount = kMaxSamplesCount;
    m_RayTracingConstants.resolutionX = resolutionWidth;
    m_RayTracingConstants.resolutionY = resolutionHeight;

    if ( !m_SceneLuminance.Init( resolutionWidth, resolutionHeight, m_FilmTexture ) )
        return false;

    if ( !m_PostProcessing.Init( resolutionWidth, resolutionHeight, m_FilmTexture, m_SceneLuminance.GetLuminanceResultBuffer() ) )
        return false;

    return true;
}

bool Scene::ResetScene()
{
    const CommandLineArgs* commandLineArgs = CommandLineArgs::Singleton();

    Mesh mesh;
    if ( !mesh.LoadFromOBJFile( commandLineArgs->GetFilename().c_str(), commandLineArgs->GetMtlFileSearchPath().c_str(), !commandLineArgs->GetNoBVHAccel() ) )
        return false;

    m_VerticesBuffer.reset( GPUBuffer::Create(
          sizeof( Vertex ) * mesh.GetVertexCount()
        , sizeof( Vertex )
        , GPUResourceCreationFlags_IsImmutable | GPUResourceCreationFlags_IsStructureBuffer
        , mesh.GetVertices() ) );
    if ( !m_VerticesBuffer )
        return false;

    m_TrianglesBuffer.reset( GPUBuffer::Create(
          sizeof( uint32_t ) * mesh.GetIndexCount()
        , sizeof( uint32_t )
        , GPUResourceCreationFlags_IsImmutable | GPUResourceCreationFlags_IsStructureBuffer
        , mesh.GetIndices() ) );
    if ( !m_TrianglesBuffer )
        return false;

    m_MaterialIdsBuffer.reset( GPUBuffer::Create(
          sizeof( uint32_t ) * mesh.GetTriangleCount()
        , sizeof( uint32_t )
        , GPUResourceCreationFlags_IsImmutable | GPUResourceCreationFlags_IsStructureBuffer
        , mesh.GetMaterialIds() ) );
    if ( !m_MaterialIdsBuffer )
        return false;

    m_MaterialsBuffer.reset( GPUBuffer::Create(
          sizeof( Material ) * mesh.GetMaterialCount()
        , sizeof( Material )
        , GPUResourceCreationFlags_IsImmutable | GPUResourceCreationFlags_IsStructureBuffer
        , mesh.GetMaterials() ) );
    if ( !m_MaterialsBuffer )
        return false;

    if ( !commandLineArgs->GetNoBVHAccel() )
    {
        m_BVHNodesBuffer.reset( GPUBuffer::Create(
              sizeof( BVHNode ) * mesh.GetBVHNodeCount()
            , sizeof( BVHNode )
            , GPUResourceCreationFlags_IsImmutable | GPUResourceCreationFlags_IsStructureBuffer
            , mesh.GetBVHNodes() ) );
        if ( !m_BVHNodesBuffer )
            return false;
    }

    m_PointLights.reserve( kMaxPointLightsCount );

    m_PointLightsBuffer.reset( GPUBuffer::Create(
          sizeof( PointLight ) * kMaxPointLightsCount
        , sizeof( PointLight )
        , GPUResourceCreationFlags_CPUWriteable | GPUResourceCreationFlags_IsStructureBuffer
        , m_PointLights.data() ) );
    if ( !m_PointLightsBuffer )
        return false;

    m_RayTracingConstants.maxBounceCount = 2;
    m_RayTracingConstants.primitiveCount = mesh.GetTriangleCount();
    m_RayTracingConstants.pointLightCount = m_PointLights.size();
    m_RayTracingConstants.filmSize = XMFLOAT2( 0.05333f, 0.03f );
    m_RayTracingConstants.filmDistance = 0.04f;
    m_RayTracingConstants.cameraTransform =
    { 1.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 1.0f, 0.0f,
      0.0f, 0.0f, 0.0f, 1.0f };
    m_RayTracingConstants.background = { 1.0f, 1.0f, 1.0f, 0.f };

    m_IsFilmDirty = true;

    UpdateRayTracingJob();

    return true;
}

void Scene::AddOneSampleAndRender()
{
    AddOneSample();
    m_SceneLuminance.Dispatch();
    m_PostProcessing.Execute( m_DefaultRenderTarget );
}

bool Scene::OnWndMessage( UINT message, WPARAM wParam, LPARAM lParam )
{
    return m_Camera.OnWndMessage( message, wParam, lParam );
}

void Scene::AddOneSample()
{
    m_Camera.Update();

    if ( m_Camera.IsDirty() )
    {
        m_Camera.GetTransformMatrixAndClearDirty( &m_RayTracingConstants.cameraTransform );
        m_IsConstantBufferDirty = true;
    }

    m_IsFilmDirty = m_IsFilmDirty || m_IsConstantBufferDirty || m_IsPointLightBufferDirty;

    if ( m_IsFilmDirty )
    {
        ClearFilmTexture();
        m_IsFilmDirty = false;
    }

    if ( UpdateResources() )
        DispatchRayTracing();
}

bool Scene::UpdateResources()
{
    if ( void* address = m_SamplesBuffer->Map() )
    {
        float* samples = reinterpret_cast< float* >( address );
        for ( int i = 0; i < kMaxSamplesCount; ++i )
        {
            samples[ i ] = m_UniformRealDistribution( m_MersenneURBG );
        }
        m_SamplesBuffer->Unmap();
    }
    else
    {
        return false;
    }

    if ( m_IsConstantBufferDirty )
    {
        if ( void* address = m_RayTracingConstantsBuffer->Map() )
        {
            memcpy( address, &m_RayTracingConstants, sizeof( m_RayTracingConstants ) );
            m_RayTracingConstantsBuffer->Unmap();
            m_IsConstantBufferDirty = false;
        }
        else
        {
            return false;
        }
    }

    if ( m_IsPointLightBufferDirty )
    {
        if ( void* address = m_PointLightsBuffer->Map() )
        {
            memcpy( address, m_PointLights.data(), sizeof( PointLight ) * m_PointLights.size() );
            m_PointLightsBuffer->Unmap();
            m_IsPointLightBufferDirty = false;
        }
        else
        {
            return false;
        }
    }

    return true;
}

void Scene::DispatchRayTracing()
{
    static const uint32_t s_SamplerCountBufferClearValue[ 4 ] = { 0 };
    GetDeviceContext()->ClearUnorderedAccessViewUint( m_SampleCounterBuffer->GetUAV(), s_SamplerCountBufferClearValue );

    m_RayTracingJob.Dispatch();
}

void Scene::ClearFilmTexture()
{
    ID3D11DeviceContext* deviceContext = GetDeviceContext();
    const static float kClearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    deviceContext->ClearRenderTargetView( m_FilmTexture->GetRTV(), kClearColor );
}

void Scene::UpdateRayTracingJob()
{
    m_RayTracingJob.m_SamplerStates = { m_UVClampSamplerState.Get() };

    m_RayTracingJob.m_UAVs = { m_FilmTexture->GetUAV(), m_SampleCounterBuffer->GetUAV() };

    m_RayTracingJob.m_SRVs = {
          m_VerticesBuffer->GetSRV()
        , m_TrianglesBuffer->GetSRV()
        , m_PointLightsBuffer->GetSRV()
        , m_SamplesBuffer->GetSRV()
        , m_CookTorranceCompETexture->GetSRV()
        , m_CookTorranceCompEAvgTexture->GetSRV()
        , m_CookTorranceCompInvCDFTexture->GetSRV()
        , m_CookTorranceCompPdfScaleTexture->GetSRV()
        , m_CookTorranceCompEFresnelTexture->GetSRV()
        , m_BVHNodesBuffer ? m_BVHNodesBuffer->GetSRV() : nullptr
        , m_MaterialIdsBuffer->GetSRV()
        , m_MaterialsBuffer->GetSRV()
        , m_EnvironmentTexture->GetSRV()
    };

    m_RayTracingJob.m_ConstantBuffers = { m_RayTracingConstantsBuffer->GetBuffer() };

    m_RayTracingJob.m_Shader = m_RayTracingShader.get();

    m_RayTracingJob.m_DispatchSizeX = (uint32_t)ceil( m_RayTracingConstants.resolutionX / 16.0f );
    m_RayTracingJob.m_DispatchSizeY = (uint32_t)ceil( m_RayTracingConstants.resolutionY / 16.0f );
    m_RayTracingJob.m_DispatchSizeZ = 1;
}

void Scene::OnImGUI()
{
    {
        ImGui::Begin( "Settings" );
        ImGui::PushItemWidth( ImGui::GetFontSize() * -12 );

        if ( ImGui::CollapsingHeader( "Film" ) )
        {
            if ( ImGui::InputFloat2( "Film Size", (float*)&m_RayTracingConstants.filmSize ) )
                m_IsConstantBufferDirty = true;

            if ( ImGui::DragFloat( "Film Distance", (float*)&m_RayTracingConstants.filmDistance, 0.005f, 0.001f, 1000.0f ) )
                m_IsConstantBufferDirty = true;
        }

        if ( ImGui::CollapsingHeader( "Kernel" ) )
        {
            if ( ImGui::DragInt( "Max Bounce Count", (int*)&m_RayTracingConstants.maxBounceCount, 0.5f, 0, 10 ) )
                m_IsConstantBufferDirty = true;
        }

        if ( ImGui::CollapsingHeader( "Environment" ) )
        {
            ImGui::SetColorEditOptions( ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR );
            if ( ImGui::ColorEdit3( "Background Color", (float*)&m_RayTracingConstants.background ) )
                m_IsConstantBufferDirty = true;
        }

        m_SceneLuminance.OnImGUI();
        m_PostProcessing.OnImGUI();

        ImGui::PopItemWidth();
        ImGui::End();
    }

    {
        ImGui::Begin( "Scene", (bool*)0, ImGuiWindowFlags_MenuBar );

        if ( ImGui::BeginMenuBar() )
        {
            if ( ImGui::BeginMenu( "Edit" ) )
            {
                if ( ImGui::BeginMenu( "Create" ) )
                {
                    if ( ImGui::MenuItem( "Point Light", "", false, m_PointLights.size() < kMaxPointLightsCount ) )
                    {
                        m_PointLights.emplace_back();
                        m_PointLights.back().color = XMFLOAT3( 1.0f, 1.0f, 1.0f );
                        m_PointLights.back().position = XMFLOAT3( 0.0f, 0.0f, 0.0f );
                        m_RayTracingConstants.pointLightCount++;
                        m_IsConstantBufferDirty = true;
                        m_IsPointLightBufferDirty = true;
                    }
                    ImGui::EndMenu();
                }
                if ( ImGui::MenuItem( "Delete", "", false, m_PointLightSelectionIndex != -1 ) )
                {
                    m_PointLights.erase( m_PointLights.begin() + m_PointLightSelectionIndex );
                    m_PointLightSelectionIndex = -1;
                    m_RayTracingConstants.pointLightCount--;
                    m_IsConstantBufferDirty = true;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        ImGui::BeginChild( "Left Pane", ImVec2( 150, 0 ), true );

        char label[ 32 ];
        for ( size_t iLight = 0; iLight < m_PointLights.size(); ++iLight )
        {
            bool isSelected = ( iLight == m_PointLightSelectionIndex );
            sprintf( label, "Point Lights %d", iLight );
            if ( ImGui::Selectable( label, isSelected ) )
                m_PointLightSelectionIndex = (int)iLight;
        }

        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginGroup();

        ImGui::BeginChild( "Inspector", ImVec2( 0, -ImGui::GetFrameHeightWithSpacing() ) );

        ImGui::SetColorEditOptions( ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR );
        ImGui::PushItemWidth( ImGui::GetFontSize() * -9 );
        if ( m_PointLightSelectionIndex >= 0 && m_PointLightSelectionIndex < m_PointLights.size() )
        {
            PointLight* selection = m_PointLights.data() + m_PointLightSelectionIndex;
            if ( ImGui::DragFloat3( "Position", (float*)&selection->position, 1.0f ) )
                m_IsPointLightBufferDirty = true;
            if ( ImGui::ColorEdit3( "Color", (float*)&selection->color ) )
                m_IsPointLightBufferDirty = true;
        }
        ImGui::PopItemWidth();

        ImGui::EndChild();

        ImGui::EndGroup();

        ImGui::End();
    }

    {
        ImGui::Begin( "Render Stats." );

        ImGui::Text( "Average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate );

        ImGui::End();
    }
}




