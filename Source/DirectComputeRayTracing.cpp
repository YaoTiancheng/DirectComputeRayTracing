#include "stdafx.h"
#include "DirectComputeRayTracing.h"
#include "D3D11RenderSystem.h"
#include "CommandLineArgs.h"
#include "Mesh.h"
#include "GPUTexture.h"
#include "GPUBuffer.h"
#include "Shader.h"
#include "Logging.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx11.h"
#include "../Shaders/RayTracingDef.inc.hlsl"
#include "../Shaders/SumLuminanceDef.inc.hlsl"

using namespace DirectX;

static const uint32_t s_MaxRayBounce = 5;

struct RayTracingFrameConstants
{
    uint32_t frameSeed;
    uint32_t padding[ 3 ];
};

CDirectComputeRayTracing::CDirectComputeRayTracing()
    : m_IsFilmDirty( true )
    , m_PointLightSelectionIndex( -1 )
    , m_MaterialSelectionIndex( -1 )
    , m_RayTracingKernelIndex( 0 )
    , m_IsConstantBufferDirty( true )
    , m_IsPointLightBufferDirty( false )
    , m_IsMaterialBufferDirty( false )
    , m_IsRayTracingJobDirty( true )
{
    std::random_device randomDevice;
}

CDirectComputeRayTracing::~CDirectComputeRayTracing()
{
}

bool CDirectComputeRayTracing::Init()
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
    
    static const char* s_RayTracingKernelDefines[ kRayTracingKernelCount ] = { NULL, "OUTPUT_NORMAL", "OUTPUT_TANGENT", "OUTPUT_ALBEDO", "OUTPUT_NEGATIVE_NDOTV", "OUTPUT_BACKFACE" };
    for ( int i = 0; i < kRayTracingKernelCount; ++i )
    {
        if ( CommandLineArgs::Singleton()->GetNoBVHAccel() )
        {
            rayTracingShaderDefines.push_back( { "NO_BVH_ACCEL", "0" } );
        }
        if ( s_RayTracingKernelDefines[ i ] )
        {
            rayTracingShaderDefines.push_back( { s_RayTracingKernelDefines[ i ], "0" } );
        }
        rayTracingShaderDefines.push_back( { NULL, NULL } );

        m_RayTracingShader[ i ].reset( ComputeShader::CreateFromFile( L"Shaders\\RayTracing.hlsl", rayTracingShaderDefines ) );
        if ( !m_RayTracingShader[ i ] )
            return false;

        rayTracingShaderDefines.clear();
    }

    m_RayTracingConstantsBuffer.reset( GPUBuffer::Create(
          sizeof( RayTracingConstants )
        , 0
        , GPUResourceCreationFlags_CPUWriteable | GPUResourceCreationFlags_IsConstantBuffer ) );
    if ( !m_RayTracingConstantsBuffer )
        return false;

    m_RayTracingFrameConstantBuffer.reset( GPUBuffer::Create(
          sizeof( RayTracingFrameConstants )
        , 0
        , GPUResourceCreationFlags_CPUWriteable | GPUResourceCreationFlags_IsConstantBuffer ) );
    if ( !m_RayTracingFrameConstantBuffer )
        return false;

    m_RenderResultTexture.reset( GPUTexture::Create( resolutionWidth, resolutionHeight, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, GPUResourceCreationFlags_IsRenderTarget ) );
    if ( !m_RenderResultTexture )
        return false;

    m_sRGBBackbuffer.reset( GPUTexture::CreateFromSwapChain( DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ) );
    if ( !m_sRGBBackbuffer)
        return false;

    m_LinearBackbuffer.reset( GPUTexture::CreateFromSwapChain() );
    if ( !m_LinearBackbuffer )
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

    m_RayTracingConstants.resolutionX = resolutionWidth;
    m_RayTracingConstants.resolutionY = resolutionHeight;

    if ( !m_SceneLuminance.Init( resolutionWidth, resolutionHeight, m_FilmTexture ) )
        return false;

    if ( !m_PostProcessing.Init( resolutionWidth, resolutionHeight, m_FilmTexture, m_RenderResultTexture, m_SceneLuminance.GetLuminanceResultBuffer() ) )
        return false;

    UpdateRenderViewport( resolutionWidth, resolutionHeight );

    return true;
}

bool CDirectComputeRayTracing::ResetScene()
{
    const CommandLineArgs* commandLineArgs = CommandLineArgs::Singleton();

    Mesh mesh;
    {
        const char* filePath = commandLineArgs->GetFilename().c_str();
        char filePathNoExtension[ MAX_PATH ];
        char fileDir[ MAX_PATH ];
        strcpy( filePathNoExtension, filePath );
        PathRemoveExtensionA( filePathNoExtension );
        const char* fileName = PathFindFileNameA( filePathNoExtension );
        strcpy( fileDir, filePath );
        PathRemoveFileSpecA( fileDir );

        char BVHFilePath[ MAX_PATH ];
        const char* MTLSearchPath = fileDir;
        sprintf_s( BVHFilePath, MAX_PATH, "%s\\%s.xml", fileDir, fileName );
        bool buildBVH = !commandLineArgs->GetNoBVHAccel();

        LOG_STRING_FORMAT( "Loading mesh from: %s, MTL search path at: %s, BVH file path at: %s\n", filePath, MTLSearchPath, BVHFilePath );

        if ( !mesh.LoadFromOBJFile( filePath, MTLSearchPath, buildBVH, BVHFilePath ) )
            return false;

        LOG_STRING_FORMAT( "Mesh loaded. Triangle count: %d, vertex count: %d, material count: %d\n", mesh.GetTriangleCount(), mesh.GetVertexCount(), mesh.GetMaterials().size() );
    }

    if ( !commandLineArgs->GetNoBVHAccel() )
    {
        uint32_t BVHMaxDepth = mesh.GetBVHMaxDepth();
        uint32_t BVHMaxStackSize = mesh.GetBVHMaxStackSize();
        LOG_STRING_FORMAT( "BVH created from mesh. Node count:%d, max depth:%d, max stack size:%d\n", mesh.GetBVHNodeCount(), BVHMaxDepth, BVHMaxStackSize );
        if ( BVHMaxStackSize > RT_BVH_TRAVERSAL_STACK_SIZE )
        {
            LOG_STRING_FORMAT( "Error: Abort because BVH max stack size %d exceeding shader BVH traversal stack size %d.\n", BVHMaxStackSize, RT_BVH_TRAVERSAL_STACK_SIZE );
            return false;
        }
    }

    m_Materials = mesh.GetMaterials();
    m_MaterialNames = mesh.GetMaterialNames();

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
          sizeof( Material ) * m_Materials.size()
        , sizeof( Material )
        , GPUResourceCreationFlags_CPUWriteable | GPUResourceCreationFlags_IsStructureBuffer
        , m_Materials.data() ) );
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

    return true;
}

void CDirectComputeRayTracing::AddOneSampleAndRender()
{
    m_FrameTimer.BeginFrame();

    AddOneSample();
    m_SceneLuminance.Dispatch();

    ID3D11DeviceContext* deviceContext = GetDeviceContext();

    ID3D11RenderTargetView* RTV = m_RenderResultTexture->GetRTV();
    deviceContext->OMSetRenderTargets( 1, &RTV, nullptr );

    D3D11_VIEWPORT viewport = { 0.0f, 0.0f, (float)m_RayTracingConstants.resolutionX, (float)m_RayTracingConstants.resolutionY, 0.0f, 1.0f };
    deviceContext->RSSetViewports( 1, &viewport );

    m_PostProcessing.ExecutePostFX();

    RTV = m_sRGBBackbuffer->GetRTV();
    deviceContext->OMSetRenderTargets( 1, &RTV, nullptr );

    DXGI_SWAP_CHAIN_DESC swapChainDesc;
    GetSwapChain()->GetDesc( &swapChainDesc );
    viewport = { 0.0f, 0.0f, (float)swapChainDesc.BufferDesc.Width, (float)swapChainDesc.BufferDesc.Height, 0.0f, 1.0f };
    deviceContext->RSSetViewports( 1, &viewport );

    XMFLOAT4 clearColor = { 0.0f, 0.0f, 0.0f, 0.0f };
    deviceContext->ClearRenderTargetView( RTV, (float*)&clearColor );

    viewport = { (float)m_RenderViewport.m_TopLeftX, (float)m_RenderViewport.m_TopLeftY, (float)m_RenderViewport.m_Width, (float)m_RenderViewport.m_Height, 0.0f, 1.0f };
    deviceContext->RSSetViewports( 1, &viewport );

    m_PostProcessing.ExecuteCopy();
}

bool CDirectComputeRayTracing::OnWndMessage( UINT message, WPARAM wParam, LPARAM lParam )
{
    if ( message == WM_SIZE )
    {
        m_sRGBBackbuffer.reset();
        m_LinearBackbuffer.reset();

        UINT width = LOWORD( lParam );
        UINT height = HIWORD( lParam );
        ResizeSwapChainBuffers( width, height );
        
        m_sRGBBackbuffer.reset( GPUTexture::CreateFromSwapChain( DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ) );
        m_LinearBackbuffer.reset( GPUTexture::CreateFromSwapChain() );

        UpdateRenderViewport( width, height );
    }
    return m_Camera.OnWndMessage( message, wParam, lParam );
}

void CDirectComputeRayTracing::AddOneSample()
{
    m_Camera.Update( m_FrameTimer.GetCurrentFrameDeltaTime() );

    if ( m_Camera.IsDirty() )
    {
        m_Camera.GetTransformMatrixAndClearDirty( &m_RayTracingConstants.cameraTransform );
        m_IsConstantBufferDirty = true;
    }

    m_IsFilmDirty = m_IsFilmDirty || m_IsConstantBufferDirty || m_IsPointLightBufferDirty || m_IsMaterialBufferDirty || m_IsRayTracingJobDirty;

    if ( m_IsFilmDirty )
    {
        ClearFilmTexture();
        m_IsFilmDirty = false;
        m_SampleCountPerPixel = 0;
    }

    if ( m_IsRayTracingJobDirty )
    {
        UpdateRayTracingJob();
        m_IsRayTracingJobDirty = false;
    }

    if ( UpdateResources() )
    {
        DispatchRayTracing();
        m_SampleCountPerPixel++;
    }
}

bool CDirectComputeRayTracing::UpdateResources()
{
    if ( void* address = m_RayTracingFrameConstantBuffer->Map() )
    {
        RayTracingFrameConstants* constants = (RayTracingFrameConstants*)address;
        constants->frameSeed = m_SampleCountPerPixel;
        m_RayTracingFrameConstantBuffer->Unmap();
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

    if ( m_IsMaterialBufferDirty )
    {
        if ( void* address = m_MaterialsBuffer->Map() )
        {
            memcpy( address, m_Materials.data(), sizeof( Material ) * m_Materials.size() );
            m_MaterialsBuffer->Unmap();
            m_IsMaterialBufferDirty = false;
        }
        else
        {
            return false;
        }
    }

    return true;
}

void CDirectComputeRayTracing::DispatchRayTracing()
{
    m_RayTracingJob.Dispatch();
}

void CDirectComputeRayTracing::ClearFilmTexture()
{
    ID3D11DeviceContext* deviceContext = GetDeviceContext();
    const static float kClearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    deviceContext->ClearRenderTargetView( m_FilmTexture->GetRTV(), kClearColor );
}

void CDirectComputeRayTracing::UpdateRayTracingJob()
{
    m_RayTracingJob.m_SamplerStates = { m_UVClampSamplerState.Get() };

    m_RayTracingJob.m_UAVs = { m_FilmTexture->GetUAV() };

    m_RayTracingJob.m_SRVs = {
          m_VerticesBuffer->GetSRV()
        , m_TrianglesBuffer->GetSRV()
        , m_PointLightsBuffer->GetSRV()
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

    m_RayTracingJob.m_ConstantBuffers = { m_RayTracingConstantsBuffer->GetBuffer(), m_RayTracingFrameConstantBuffer->GetBuffer() };

    m_RayTracingJob.m_Shader = m_RayTracingShader[ m_RayTracingKernelIndex ].get();

    m_RayTracingJob.m_DispatchSizeX = (uint32_t)ceil( m_RayTracingConstants.resolutionX / 16.0f );
    m_RayTracingJob.m_DispatchSizeY = (uint32_t)ceil( m_RayTracingConstants.resolutionY / 16.0f );
    m_RayTracingJob.m_DispatchSizeZ = 1;
}

void CDirectComputeRayTracing::UpdateRenderViewport( uint32_t backbufferWidth, uint32_t backbufferHeight )
{
    uint32_t renderWidth = m_RayTracingConstants.resolutionX;
    uint32_t renderHeight = m_RayTracingConstants.resolutionY;
    float scale = (float)backbufferWidth / renderWidth;
    float desiredViewportHeight = renderHeight * scale;
    if ( desiredViewportHeight > backbufferHeight )
    {
        scale = (float)backbufferHeight / renderHeight;
    }
    
    m_RenderViewport.m_Width    = renderWidth * scale;
    m_RenderViewport.m_Height   = renderHeight * scale;
    m_RenderViewport.m_TopLeftX = ( backbufferWidth - m_RenderViewport.m_Width ) * 0.5f;
    m_RenderViewport.m_TopLeftY = ( backbufferHeight - m_RenderViewport.m_Height ) * 0.5f;
}

void CDirectComputeRayTracing::OnImGUI()
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
            static const char* s_KernelTypeNames[ kRayTracingKernelCount ] = { "Path Tracing", "Output Normal", "Output Tangent", "Output Albedo", "Output Negative NdotV", "Output Backface" };
            if ( ImGui::Combo( "Type", &m_RayTracingKernelIndex, s_KernelTypeNames, IM_ARRAYSIZE( s_KernelTypeNames ) ) )
            {
                m_IsRayTracingJobDirty = true;

                m_PostProcessing.SetPostFXDisable( m_RayTracingKernelIndex != 0 );
            }
            if ( m_RayTracingKernelIndex == 0 )
            {
                if ( ImGui::DragInt( "Max Bounce Count", (int*)&m_RayTracingConstants.maxBounceCount, 0.5f, 0, s_MaxRayBounce ) )
                    m_IsConstantBufferDirty = true;
            }
        }

        if ( m_RayTracingKernelIndex == 0 )
        {
            if ( ImGui::CollapsingHeader( "Environment" ) )
            {
                ImGui::SetColorEditOptions( ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR );
                if ( ImGui::ColorEdit3( "Background Color", (float*)&m_RayTracingConstants.background ) )
                    m_IsConstantBufferDirty = true;
            }

            m_SceneLuminance.OnImGUI();
            m_PostProcessing.OnImGUI();
        }

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

        char label[ 32 ];
        for ( size_t iLight = 0; iLight < m_PointLights.size(); ++iLight )
        {
            bool isSelected = ( iLight == m_PointLightSelectionIndex );
            sprintf( label, "Point Lights %d", iLight );
            if ( ImGui::Selectable( label, isSelected ) )
            {
                m_PointLightSelectionIndex = (int)iLight;
                m_MaterialSelectionIndex = -1;
            }
        }

        ImGui::End();
    }

    {
        ImGui::Begin( "Materials" );

        for ( size_t iMaterial = 0; iMaterial < m_Materials.size(); ++iMaterial )
        {
            bool isSelected = ( iMaterial == m_MaterialSelectionIndex );
            if ( ImGui::Selectable( m_MaterialNames[ iMaterial ].c_str(), isSelected ) )
            {
                m_MaterialSelectionIndex = (int)iMaterial;
                m_PointLightSelectionIndex = -1;
            }
        }

        ImGui::End();
    }

    {
        ImGui::Begin( "Inspector" );

        ImGui::PushItemWidth( ImGui::GetFontSize() * -9 );

        if ( m_PointLightSelectionIndex >= 0 )
        {
            ImGui::SetColorEditOptions( ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR );
            if ( m_PointLightSelectionIndex < m_PointLights.size() )
            {
                PointLight* selection = m_PointLights.data() + m_PointLightSelectionIndex;
                if ( ImGui::DragFloat3( "Position", (float*)&selection->position, 1.0f ) )
                    m_IsPointLightBufferDirty = true;
                if ( ImGui::ColorEdit3( "Color", (float*)&selection->color ) )
                    m_IsPointLightBufferDirty = true;
            }
        }
        else if ( m_MaterialSelectionIndex >= 0 )
        {
            if ( m_MaterialSelectionIndex < m_Materials.size() )
            {
                Material* selection = m_Materials.data() + m_MaterialSelectionIndex;
                ImGui::SetColorEditOptions( ImGuiColorEditFlags_Float );
                if ( ImGui::ColorEdit3( "Albedo", (float*)&selection->albedo ) )
                    m_IsMaterialBufferDirty = true;
                ImGui::SetColorEditOptions( ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR );
                if ( ImGui::ColorEdit3( "Emission", (float*)&selection->emission ) )
                    m_IsMaterialBufferDirty = true;
                if ( ImGui::DragFloat( "Roughness", &selection->roughness, 0.01f, 0.001f, 1.0f ) )
                    m_IsMaterialBufferDirty = true;
                if ( ImGui::DragFloat( "IOR", &selection->ior, 0.01f, 1.0f, 3.0f ) )
                    m_IsMaterialBufferDirty = true;
            }
        }

        ImGui::End();
    }

    {
        ImGui::Begin( "Preview Camera" );

        ImGui::PushItemWidth( ImGui::GetFontSize() * -9 );

        m_Camera.OnImGUI();

        ImGui::End();
    }

    {
        ImGui::Begin( "Render Stats." );

        ImGui::Text( "Average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate );

        ImGui::End();
    }

    // Rendering
    ImGui::Render();

    ID3D11RenderTargetView* RTV = m_LinearBackbuffer->GetRTV();
    GetDeviceContext()->OMSetRenderTargets( 1, &RTV, nullptr );

    ImGui_ImplDX11_RenderDrawData( ImGui::GetDrawData() );

    RTV = nullptr;
    GetDeviceContext()->OMSetRenderTargets( 1, &RTV, nullptr );
}




