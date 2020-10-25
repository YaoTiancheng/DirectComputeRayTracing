#include "stdafx.h"
#include "Scene.h"
#include "D3D11RenderSystem.h"
#include "CommandLineArgs.h"
#include "Mesh.h"
#include "../Shaders/PointLight.inc.hlsl"
#include "../Shaders/SumLuminanceDef.inc.hlsl"

using namespace DirectX;

XMFLOAT4 kScreenQuadVertices[ 6 ] =
{
    { -1.0f,  1.0f,  0.0f,  1.0f },
    {  1.0f,  1.0f,  0.0f,  1.0f },
    { -1.0f, -1.0f,  0.0f,  1.0f },
    { -1.0f, -1.0f,  0.0f,  1.0f },
    {  1.0f,  1.0f,  0.0f,  1.0f },
    {  1.0f, -1.0f,  0.0f,  1.0f },
};

D3D11_INPUT_ELEMENT_DESC kScreenQuadInputElementDesc[ 1 ]
{
    { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
};

Scene::Scene()
    : m_IsFilmDirty( true )
    , m_UniformRealDistribution( 0.0f, std::nexttoward( 1.0f, 0.0f ) )
{
    std::random_device randomDevice;
    m_MersenneURBG = std::mt19937( randomDevice() );
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

    {
        std::vector<D3D_SHADER_MACRO> sumLuminanceShaderDefines;
        sumLuminanceShaderDefines.push_back( { NULL, NULL } );
        m_SumLuminanceToSingleShader.reset( ComputeShader::CreateFromFile( L"Shaders\\SumLuminance.hlsl", sumLuminanceShaderDefines ) );
        if ( !m_SumLuminanceToSingleShader )
            return false;

        sumLuminanceShaderDefines.insert( sumLuminanceShaderDefines.begin(), { "REDUCE_TO_1D", "0" } );
        m_SumLuminanceTo1DShader.reset( ComputeShader::CreateFromFile( L"Shaders\\SumLuminance.hlsl", sumLuminanceShaderDefines ) );
        if ( !m_SumLuminanceTo1DShader )
            return false;
    }

    {
        m_SumLuminanceBlockCountX = uint32_t( std::ceilf( resolutionWidth / float( SL_BLOCKSIZE ) ) );
        m_SumLuminanceBlockCountX = uint32_t( std::ceilf( m_SumLuminanceBlockCountX / 2.0f ) );
        m_SumLuminanceBlockCountY = uint32_t( std::ceilf( resolutionHeight / float( SL_BLOCKSIZEY ) ) );
        m_SumLuminanceBlockCountY = uint32_t( std::ceilf( m_SumLuminanceBlockCountY / 2.0f ) );
        m_SumLuminanceBuffer0.reset( GPUBuffer::Create(
              sizeof( float ) * m_SumLuminanceBlockCountX * m_SumLuminanceBlockCountY
            , sizeof( float )
            , GPUResourceCreationFlags_IsStructureBuffer | GPUResourceCreationFlags_HasUAV ) );
        if ( !m_SumLuminanceBuffer0 )
            return false;
        m_SumLuminanceBuffer1.reset( GPUBuffer::Create(
              sizeof( float ) * m_SumLuminanceBlockCountX * m_SumLuminanceBlockCountY
            , sizeof( float )
            , GPUResourceCreationFlags_IsStructureBuffer | GPUResourceCreationFlags_HasUAV ) );
        if ( !m_SumLuminanceBuffer1 )
            return false;
    }

    {
        uint32_t params[ 4 ] = { m_SumLuminanceBlockCountX, m_SumLuminanceBlockCountY, resolutionWidth, resolutionHeight };
        m_SumLuminanceConstantsBuffer0.reset( GPUBuffer::Create(
              sizeof( uint32_t ) * 4
            , 0
            , GPUResourceCreationFlags_IsImmutable | GPUResourceCreationFlags_IsConstantBuffer
            , params ) );
        if ( !m_SumLuminanceConstantsBuffer0 )
            return false;

        m_SumLuminanceConstantsBuffer1.reset( GPUBuffer::Create(
              sizeof( uint32_t ) * 4
            , 0
            , GPUResourceCreationFlags_CPUWriteable | GPUResourceCreationFlags_IsConstantBuffer ) );
        if ( !m_SumLuminanceConstantsBuffer1 )
            return false;
    }

    m_RayTracingConstantsBuffer.reset( GPUBuffer::Create(
          sizeof( RayTracingConstants )
        , 0
        , GPUResourceCreationFlags_CPUWriteable | GPUResourceCreationFlags_IsConstantBuffer ) );
    if ( !m_RayTracingConstantsBuffer )
        return false;

    {
        XMFLOAT4 params = XMFLOAT4( 1.0f / ( resolutionWidth * resolutionHeight ), 0.0f, 0.0f, 0.0f );
        m_PostProcessingConstantsBuffer.reset( GPUBuffer::Create(
              sizeof( XMFLOAT4 )
            , 0
            , GPUResourceCreationFlags_IsImmutable | GPUResourceCreationFlags_IsConstantBuffer
            , &params ) );
        if ( !m_PostProcessingConstantsBuffer )
            return false;
    }

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

    m_ScreenQuadVerticesBuffer.reset( GPUBuffer::Create(
        sizeof( kScreenQuadVertices )
        , sizeof( XMFLOAT4 )
        , GPUResourceCreationFlags_IsImmutable | GPUResourceCreationFlags_IsVertexBuffer
        , &kScreenQuadVertices ) );
    if ( !m_ScreenQuadVerticesBuffer )
        return false;

    std::vector<D3D_SHADER_MACRO> postProcessingShaderDefines;
    postProcessingShaderDefines.push_back( { NULL, NULL } );
    m_PostFXShader.reset( GfxShader::CreateFromFile( L"Shaders\\PostProcessings.hlsl", postProcessingShaderDefines ) );
    if ( !m_PostFXShader )
        return false;

    m_ScreenQuadVertexInputLayout.Attach( m_PostFXShader->CreateInputLayout( kScreenQuadInputElementDesc, 1 ) );
    if ( !m_ScreenQuadVertexInputLayout )
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
    HRESULT hr = device->CreateSamplerState( &samplerDesc, &m_CopySamplerState );
    if ( FAILED( hr ) )
        return false;

    samplerDesc.Filter = D3D11_FILTER_MAXIMUM_MIN_MAG_MIP_POINT;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MaxAnisotropy = 1;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    hr = device->CreateSamplerState( &samplerDesc, &m_UVClampSamplerState );
    if ( FAILED( hr ) )
        return false;

    m_DefaultViewport = { 0.0f, 0.0f, ( float ) resolutionWidth, ( float ) resolutionHeight, 0.0f, 1.0f };
    m_RayTracingConstants.samplesCount = kMaxSamplesCount;
    m_RayTracingConstants.resolutionX = resolutionWidth;
    m_RayTracingConstants.resolutionY = resolutionHeight;

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

    uint32_t pointLightCount = 1;
    std::vector<PointLight> pointLights( pointLightCount );
    pointLights[ 0 ].position = XMFLOAT3( 4.0f, 9.0f, -5.0f );
    pointLights[ 0 ].color = XMFLOAT3( 200.0f, 200.0f, 200.0f );

    m_PointLightsBuffer.reset( GPUBuffer::Create(
        sizeof( PointLight ) * pointLightCount
        , sizeof( PointLight )
        , GPUResourceCreationFlags_IsImmutable | GPUResourceCreationFlags_IsStructureBuffer
        , pointLights.data() ) );
    if ( !m_PointLightsBuffer )
        return false;

    m_RayTracingConstants.maxBounceCount = 2;
    m_RayTracingConstants.primitiveCount = mesh.GetTriangleCount();
    m_RayTracingConstants.pointLightCount = pointLightCount;
    m_RayTracingConstants.filmSize = XMFLOAT2( 0.05333f, 0.03f );
    m_RayTracingConstants.filmDistance = 0.04f;
    m_RayTracingConstants.cameraTransform =
    { 1.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 1.0f, 0.0f,
      0.0f, 0.0f, 0.0f, 1.0f };
    m_RayTracingConstants.background = { 1.0f, 1.0f, 1.0f, 0.f };

    m_IsFilmDirty = true;

    UpdatePostProcessingJob();
    UpdateRayTracingJob();
    UpdateSumLuminanceJobs();

    return true;
}

void Scene::AddOneSampleAndRender()
{
    AddOneSample();
    DispatchSumLuminance();
    DoPostProcessing();
}

bool Scene::OnWndMessage( UINT message, WPARAM wParam, LPARAM lParam )
{
    return m_Camera.OnWndMessage( message, wParam, lParam );
}

void Scene::AddOneSample()
{
    m_Camera.Update();

    if ( m_Camera.IsDirty() || m_IsFilmDirty )
    {
        ClearFilmTexture();
        m_IsFilmDirty = false;

        if ( m_Camera.IsDirty() )
            m_Camera.GetTransformMatrixAndClearDirty( &m_RayTracingConstants.cameraTransform );
    }

    if ( UpdateResources() )
        DispatchRayTracing();
}

void Scene::DoPostProcessing()
{
    ID3D11DeviceContext* deviceContext = GetDeviceContext();

    ID3D11RenderTargetView* rawDefaultRenderTargetView = m_DefaultRenderTarget->GetRTV();
    deviceContext->OMSetRenderTargets( 1, &rawDefaultRenderTargetView, nullptr );

    deviceContext->RSSetViewports( 1, &m_DefaultViewport );

    m_PostProcessingJob.Dispatch();
}

bool Scene::UpdateResources()
{
    if ( void* address = m_RayTracingConstantsBuffer->Map() )
    {
        memcpy( address, &m_RayTracingConstants, sizeof( m_RayTracingConstants ) );
        m_RayTracingConstantsBuffer->Unmap();
    }
    else
    {
        return false;
    }

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

    return true;
}

void Scene::DispatchSumLuminance()
{
    m_SumLuminanceTo1DJob.Dispatch();

    uint32_t blockCount = m_SumLuminanceBlockCountX * m_SumLuminanceBlockCountY;
    while ( blockCount != 1 )
    {
        m_SumLuminanceToSingleJob.m_UAVs[ 0 ] = m_SumLuminanceBuffer0->GetUAV();
        m_SumLuminanceToSingleJob.m_SRVs[ 0 ] = m_SumLuminanceBuffer1->GetSRV();

        uint32_t threadGroupCount = uint32_t( std::ceilf( blockCount / float( SL_REDUCE_TO_SINGLE_GROUPTHREADS ) ) );

        if ( void* address = m_SumLuminanceConstantsBuffer1->Map() )
        {
            uint32_t* params = (uint32_t*)address;
            params[ 0 ] = blockCount;
            params[ 1 ] = threadGroupCount;
            m_SumLuminanceConstantsBuffer1->Unmap();
        }

        m_SumLuminanceToSingleJob.m_DispatchSizeX = threadGroupCount;

        m_SumLuminanceToSingleJob.Dispatch();

        blockCount = threadGroupCount;

        std::swap( m_SumLuminanceBuffer0, m_SumLuminanceBuffer1 );
    }
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

void Scene::UpdateSumLuminanceJobs()
{
    m_SumLuminanceTo1DJob.m_UAVs.push_back( m_SumLuminanceBuffer1->GetUAV() );
    m_SumLuminanceTo1DJob.m_SRVs.push_back( m_FilmTexture->GetSRV() );
    m_SumLuminanceTo1DJob.m_ConstantBuffers.push_back( m_SumLuminanceConstantsBuffer0->GetBuffer() );
    m_SumLuminanceTo1DJob.m_Shader = m_SumLuminanceTo1DShader.get();
    m_SumLuminanceTo1DJob.m_DispatchSizeX = m_SumLuminanceBlockCountX;
    m_SumLuminanceTo1DJob.m_DispatchSizeY = m_SumLuminanceBlockCountY;
    m_SumLuminanceTo1DJob.m_DispatchSizeZ = 1;

    m_SumLuminanceToSingleJob.m_UAVs.push_back( nullptr );
    m_SumLuminanceToSingleJob.m_SRVs.push_back( nullptr );
    m_SumLuminanceToSingleJob.m_ConstantBuffers.push_back( m_SumLuminanceConstantsBuffer1->GetBuffer() );
    m_SumLuminanceToSingleJob.m_Shader = m_SumLuminanceToSingleShader.get();
    m_SumLuminanceToSingleJob.m_DispatchSizeY = 1;
    m_SumLuminanceToSingleJob.m_DispatchSizeZ = 1;
}

void Scene::UpdatePostProcessingJob()
{
    m_PostProcessingJob.m_SamplerStates.push_back( m_CopySamplerState.Get() );
    m_PostProcessingJob.m_SRVs.push_back( m_FilmTexture->GetSRV() );
    m_PostProcessingJob.m_SRVs.push_back( m_SumLuminanceBuffer1->GetSRV() );
    m_PostProcessingJob.m_ConstantBuffers.push_back( m_PostProcessingConstantsBuffer->GetBuffer() );
    m_PostProcessingJob.m_Shader = m_PostFXShader.get();
    m_PostProcessingJob.m_VertexBuffer = m_ScreenQuadVerticesBuffer.get();
    m_PostProcessingJob.m_InputLayout = m_ScreenQuadVertexInputLayout.Get();
    m_PostProcessingJob.m_VertexCount = 6;
    m_PostProcessingJob.m_VertexStride = sizeof( XMFLOAT4 );
}




