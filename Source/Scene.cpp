#include "stdafx.h"
#include "Scene.h"
#include "D3D11RenderSystem.h"
#include "CommandLineArgs.h"

using namespace DirectX;

struct CookTorranceCompTextureConstants
{
    XMFLOAT4 compETextureSize;
    XMFLOAT4 compEAvgTextureSize;
    XMFLOAT4 compInvCDFTextureSize;
    XMFLOAT4 compPdfScaleTextureSize;
    XMFLOAT4 compEFresnelTextureSize;
    XMFLOAT4 compEFresnelTextureSizeRcp;
};

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

    m_RayTracingShader.reset( ComputeShader::CreateFromFile( L"Shaders\\RayTracing.hlsl" ) );
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

    // Fill in the subresource data.
    CookTorranceCompTextureConstants cooktorranceCompTextureConstants;
    cooktorranceCompTextureConstants.compETextureSize           = XMFLOAT4( 32.0f, 32.0f, 1.0f / 32.0f, 1.0f / 32.0f );
    cooktorranceCompTextureConstants.compEAvgTextureSize        = XMFLOAT4( 32.0f, 1.0f, 1.0f / 32.0f, 1.0f );
    cooktorranceCompTextureConstants.compInvCDFTextureSize      = XMFLOAT4( 32.0f, 32.0f, 1.0f / 32.0f, 1.0f / 32.0f );
    cooktorranceCompTextureConstants.compPdfScaleTextureSize    = XMFLOAT4( 32.0f, 1.0f, 1.0f / 32.0f, 1.0f );
    cooktorranceCompTextureConstants.compEFresnelTextureSize    = XMFLOAT4( 32.0f, 16.0f, 16.0f, 0.0f );
    cooktorranceCompTextureConstants.compEFresnelTextureSizeRcp = XMFLOAT4( 1.0f / 32.0f, 1.0f / 16.0f, 1.0f / 16.0f, 0.0f );
    m_CookTorranceCompTextureConstantsBuffer.reset( GPUBuffer::Create( 
          sizeof( CookTorranceCompTextureConstants )
        , 0
        , GPUResourceCreationFlags_IsImmutable | GPUResourceCreationFlags_IsConstantBuffer
        , &cooktorranceCompTextureConstants ) );
    if ( !m_CookTorranceCompTextureConstantsBuffer )
        return false;

    m_ScreenQuadVerticesBuffer.reset( GPUBuffer::Create(
          sizeof( kScreenQuadVertices )
        , sizeof( XMFLOAT4 )
        , GPUResourceCreationFlags_IsImmutable | GPUResourceCreationFlags_IsVertexBuffer
        , &kScreenQuadVertices ) );
    if ( !m_ScreenQuadVerticesBuffer )
        return false;

    m_PostFXShader.reset( GfxShader::CreateFromFile( L"Shaders\\PostProcessings.hlsl" ) );
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
    uint32_t vertexCount = 20;
    std::vector<Vertex> vertices( vertexCount );
    vertices[ 0 ].position = XMFLOAT3( -1.0f, 0.0f, 1.0f );
    vertices[ 0 ].normal = XMFLOAT3( 0.0f, 1.0f, 0.0f );
    vertices[ 0 ].tangent = XMFLOAT3( 1.0f, 0.0f, 0.0f );
    vertices[ 1 ].position = XMFLOAT3( 1.0f, 0.0f, 1.0f );
    vertices[ 1 ].normal = XMFLOAT3( 0.0f, 1.0f, 0.0f );
    vertices[ 1 ].tangent = XMFLOAT3( 1.0f, 0.0f, 0.0f );
    vertices[ 2 ].position = XMFLOAT3( 1.0f, 0.0f, -1.0f );
    vertices[ 2 ].normal = XMFLOAT3( 0.0f, 1.0f, 0.0f );
    vertices[ 2 ].tangent = XMFLOAT3( 1.0f, 0.0f, 0.0f );
    vertices[ 3 ].position = XMFLOAT3( -1.0f, 0.0f, -1.0f );
    vertices[ 3 ].normal = XMFLOAT3( 0.0f, 1.0f, 0.0f );
    vertices[ 3 ].tangent = XMFLOAT3( 1.0f, 0.0f, 0.0f );

    vertices[ 4 ].position = XMFLOAT3( -1.0f, 0.0f, 1.0f );
    vertices[ 4 ].normal = XMFLOAT3( 0.0f, 0.0f, -1.0f );
    vertices[ 4 ].tangent = XMFLOAT3( 1.0f, 0.0f, 0.0f );
    vertices[ 5 ].position = XMFLOAT3( 1.0f, 0.0f, 1.0f );
    vertices[ 5 ].normal = XMFLOAT3( 0.0f, 0.0f, -1.0f );
    vertices[ 5 ].tangent = XMFLOAT3( 1.0f, 0.0f, 0.0f );
    vertices[ 6 ].position = XMFLOAT3( -1.0f, 2.0f, 1.0f );
    vertices[ 6 ].normal = XMFLOAT3( 0.0f, 0.0f, -1.0f );
    vertices[ 6 ].tangent = XMFLOAT3( 1.0f, 0.0f, 0.0f );
    vertices[ 7 ].position = XMFLOAT3( 1.0f, 2.0f, 1.0f );
    vertices[ 7 ].normal = XMFLOAT3( 0.0f, 0.0f, -1.0f );
    vertices[ 7 ].tangent = XMFLOAT3( 1.0f, 0.0f, 0.0f );

    vertices[ 8 ].position = XMFLOAT3( -1.0f, 2.0f, 1.0f );
    vertices[ 8 ].normal = XMFLOAT3( 0.0f, -1.0f, 0.0f );
    vertices[ 8 ].tangent = XMFLOAT3( 1.0f, 0.0f, 0.0f );
    vertices[ 9 ].position = XMFLOAT3( 1.0f, 2.0f, 1.0f );
    vertices[ 9 ].normal = XMFLOAT3( 0.0f, -1.0f, 0.0f );
    vertices[ 9 ].tangent = XMFLOAT3( 1.0f, 0.0f, 0.0f );
    vertices[ 10 ].position = XMFLOAT3( 1.0f, 2.0f, -1.0f );
    vertices[ 10 ].normal = XMFLOAT3( 0.0f, -1.0f, 0.0f );
    vertices[ 10 ].tangent = XMFLOAT3( 1.0f, 0.0f, 0.0f );
    vertices[ 11 ].position = XMFLOAT3( -1.0f, 2.0f, -1.0f );
    vertices[ 11 ].normal = XMFLOAT3( 0.0f, -1.0f, 0.0f );
    vertices[ 11 ].tangent = XMFLOAT3( 1.0f, 0.0f, 0.0f );

    vertices[ 12 ].position = XMFLOAT3( -1.0f, 0.0f, 1.0f );
    vertices[ 12 ].normal = XMFLOAT3( 1.0f, 0.0f, 0.0f );
    vertices[ 12 ].tangent = XMFLOAT3( 0.0f, 1.0f, 0.0f );
    vertices[ 13 ].position = XMFLOAT3( -1.0f, 0.0f, -1.0f );
    vertices[ 13 ].normal = XMFLOAT3( 1.0f, 0.0f, 0.0f );
    vertices[ 13 ].tangent = XMFLOAT3( 0.0f, 1.0f, 0.0f );
    vertices[ 14 ].position = XMFLOAT3( -1.0f, 2.0f, -1.0f );
    vertices[ 14 ].normal = XMFLOAT3( 1.0f, 0.0f, 0.0f );
    vertices[ 14 ].tangent = XMFLOAT3( 0.0f, 1.0f, 0.0f );
    vertices[ 15 ].position = XMFLOAT3( -1.0f, 2.0f, 1.0f );
    vertices[ 15 ].normal = XMFLOAT3( 1.0f, 0.0f, 0.0f );
    vertices[ 15 ].tangent = XMFLOAT3( 0.0f, 1.0f, 0.0f );

    vertices[ 16 ].position = XMFLOAT3( 1.0f, 0.0f, 1.0f );
    vertices[ 16 ].normal = XMFLOAT3( -1.0f, 0.0f, 0.0f );
    vertices[ 16 ].tangent = XMFLOAT3( 0.0f, 1.0f, 0.0f );
    vertices[ 17 ].position = XMFLOAT3( 1.0f, 0.0f, -1.0f );
    vertices[ 17 ].normal = XMFLOAT3( -1.0f, 0.0f, 0.0f );
    vertices[ 17 ].tangent = XMFLOAT3( 0.0f, 1.0f, 0.0f );
    vertices[ 18 ].position = XMFLOAT3( 1.0f, 2.0f, -1.0f );
    vertices[ 18 ].normal = XMFLOAT3( -1.0f, 0.0f, 0.0f );
    vertices[ 18 ].tangent = XMFLOAT3( 0.0f, 1.0f, 0.0f );
    vertices[ 19 ].position = XMFLOAT3( 1.0f, 2.0f, 1.0f );
    vertices[ 19 ].normal = XMFLOAT3( -1.0f, 0.0f, 0.0f );
    vertices[ 19 ].tangent = XMFLOAT3( 0.0f, 1.0f, 0.0f );

    m_VerticesBuffer.reset( GPUBuffer::Create(
          sizeof( Vertex ) * vertexCount
        , sizeof( Vertex )
        , GPUResourceCreationFlags_IsImmutable | GPUResourceCreationFlags_IsStructureBuffer
        , vertices.data() ) );
    if ( !m_VerticesBuffer )
        return false;

    uint32_t triangleCount = 10;
    uint32_t vertexIndexCount = triangleCount * 3;
    std::vector<uint32_t> triangles( vertexIndexCount );
    triangles[ 0 ] = 0;
    triangles[ 1 ] = 1;
    triangles[ 2 ] = 2;

    triangles[ 3 ] = 0;
    triangles[ 4 ] = 2;
    triangles[ 5 ] = 3;

    triangles[ 6 ] = 5;
    triangles[ 7 ] = 4;
    triangles[ 8 ] = 6;

    triangles[ 9 ] = 5;
    triangles[ 10 ] = 6;
    triangles[ 11 ] = 7;

    triangles[ 12 ] = 9;
    triangles[ 13 ] = 8;
    triangles[ 14 ] = 10;

    triangles[ 15 ] = 10;
    triangles[ 16 ] = 8;
    triangles[ 17 ] = 11;

    triangles[ 18 ] = 12;
    triangles[ 19 ] = 13;
    triangles[ 20 ] = 15;

    triangles[ 21 ] = 13;
    triangles[ 22 ] = 14;
    triangles[ 23 ] = 15;

    triangles[ 24 ] = 17;
    triangles[ 25 ] = 16;
    triangles[ 26 ] = 19;

    triangles[ 27 ] = 19;
    triangles[ 28 ] = 18;
    triangles[ 29 ] = 17;

    std::vector<uint32_t> reorderTriangles( vertexIndexCount );
    std::vector<UnpackedBVHNode> bvhNodes;
    BuildBVH( vertices.data(), triangles.data(), reorderTriangles.data(), triangleCount, &bvhNodes );

    m_TrianglesBuffer.reset( GPUBuffer::Create( 
          sizeof( uint32_t ) * vertexIndexCount
        , sizeof( uint32_t )
        , GPUResourceCreationFlags_IsImmutable | GPUResourceCreationFlags_IsStructureBuffer
        , reorderTriangles.data() ) );
    if ( !m_TrianglesBuffer )
        return false;

    std::vector<PackedBVHNode> packedBvhNodes( bvhNodes.size() );
    PackBVH( bvhNodes.data(), uint32_t( bvhNodes.size() ), packedBvhNodes.data() );

    m_BVHNodesBuffer.reset( GPUBuffer::Create( 
          sizeof( PackedBVHNode ) * bvhNodes.size()
        , sizeof( PackedBVHNode )
        , GPUResourceCreationFlags_IsImmutable | GPUResourceCreationFlags_IsStructureBuffer
        , packedBvhNodes.data() ) );
    if ( !m_BVHNodesBuffer )
        return false;

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

    m_RayTracingConstants.maxBounceCount = 3;
    m_RayTracingConstants.primitiveCount = triangleCount;
    m_RayTracingConstants.pointLightCount = pointLightCount;
    m_RayTracingConstants.filmSize = XMFLOAT2( 0.05333f, 0.03f );
    m_RayTracingConstants.filmDistance = 0.04f;
    m_RayTracingConstants.cameraTransform =
    { 1.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 1.0f, 0.0f,
      0.0f, 0.0f, 0.0f, 1.0f };
    m_RayTracingConstants.background = { 0.0f, 0.0f, 0.0f, 0.f };

    m_IsFilmDirty = true;

    return true;
}

void Scene::AddOneSampleAndRender()
{
    AddOneSample();
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

    UINT vertexStride = sizeof( XMFLOAT4 );
    UINT vertexOffset = 0;
    ID3D11Buffer* rawScreenQuadVertexBuffer = m_ScreenQuadVerticesBuffer->GetBuffer();
    deviceContext->IASetVertexBuffers( 0, 1, &rawScreenQuadVertexBuffer, &vertexStride, &vertexOffset );
    deviceContext->IASetInputLayout( m_ScreenQuadVertexInputLayout.Get() );
    deviceContext->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );

    ID3D11RenderTargetView* rawDefaultRenderTargetView = m_DefaultRenderTarget->GetRTV();
    deviceContext->OMSetRenderTargets( 1, &rawDefaultRenderTargetView, nullptr );

    deviceContext->RSSetViewports( 1, &m_DefaultViewport );

    deviceContext->VSSetShader( m_PostFXShader->GetVertexShader(), nullptr, 0 );
    deviceContext->PSSetShader( m_PostFXShader->GetPixelShader(), nullptr, 0 );

    ID3D11SamplerState* rawCopySamplerState = m_CopySamplerState.Get();
    deviceContext->PSSetSamplers( 0, 1, &rawCopySamplerState );
    ID3D11ShaderResourceView* rawFilmTextureSRV = m_FilmTexture->GetSRV();
    deviceContext->PSSetShaderResources( 0, 1, &rawFilmTextureSRV );

    deviceContext->Draw( 6, 0 );

    rawFilmTextureSRV = nullptr;
    deviceContext->PSSetShaderResources( 0, 1, &rawFilmTextureSRV );
    rawCopySamplerState = nullptr;
    deviceContext->PSSetSamplers( 0, 1, &rawCopySamplerState );
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

void Scene::DispatchRayTracing()
{
    ID3D11DeviceContext* deviceContext = GetDeviceContext();

    deviceContext->CSSetShader( m_RayTracingShader->GetNative(), nullptr, 0 );

    ID3D11UnorderedAccessView* rawFilmTextureUAV = m_FilmTexture->GetUAV();
    deviceContext->CSSetUnorderedAccessViews( 0, 1, &rawFilmTextureUAV, nullptr );

    ID3D11SamplerState* rawUVClampSamplerState = m_UVClampSamplerState.Get();
    deviceContext->CSSetSamplers( 0, 1, &rawUVClampSamplerState );

    ID3D11ShaderResourceView* rawSRVs[] =
    {
          m_VerticesBuffer->GetSRV()
        , m_TrianglesBuffer->GetSRV()
        , m_PointLightsBuffer->GetSRV()
        , m_SamplesBuffer->GetSRV()
        , m_CookTorranceCompETexture->GetSRV()
        , m_CookTorranceCompEAvgTexture->GetSRV()
        , m_CookTorranceCompInvCDFTexture->GetSRV()
        , m_CookTorranceCompPdfScaleTexture->GetSRV()
        , m_CookTorranceCompEFresnelTexture->GetSRV()
        , m_BVHNodesBuffer->GetSRV()
    };
    deviceContext->CSSetShaderResources( 0, 10, rawSRVs );

    ID3D11Buffer* rawConstantBuffers[] = { m_RayTracingConstantsBuffer->GetBuffer(), m_CookTorranceCompTextureConstantsBuffer->GetBuffer() };
    deviceContext->CSSetConstantBuffers( 0, 2, rawConstantBuffers );

    UINT threadGroupCountX = ( UINT ) ceil( m_RayTracingConstants.resolutionX / 16.0f );
    UINT threadGroupCountY = ( UINT ) ceil( m_RayTracingConstants.resolutionY / 16.0f );
    deviceContext->Dispatch( threadGroupCountX, threadGroupCountY, 1 );

    rawFilmTextureUAV = nullptr;
    deviceContext->CSSetUnorderedAccessViews( 0, 1, &rawFilmTextureUAV, nullptr );

    rawUVClampSamplerState = nullptr;
    deviceContext->PSSetSamplers( 0, 1, &rawUVClampSamplerState );
}

void Scene::ClearFilmTexture()
{
    ID3D11DeviceContext* deviceContext = GetDeviceContext();
    const static float kClearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    deviceContext->ClearRenderTargetView( m_FilmTexture->GetRTV(), kClearColor );
}




