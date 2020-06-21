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

    m_VerticesBuffer.reset( GPUBuffer::Create( 
          sizeof( m_Vertices )
        , sizeof( Vertex )
        , GPUResourceCreationFlags_CPUWriteable | GPUResourceCreationFlags_IsStructureBuffer ) );
    if ( !m_VerticesBuffer )
        return false;

    m_TrianglesBuffer.reset( GPUBuffer::Create( 
          sizeof( m_Triangles )
        , sizeof( uint32_t )
        , GPUResourceCreationFlags_CPUWriteable | GPUResourceCreationFlags_IsStructureBuffer ) );
    if ( !m_TrianglesBuffer )
        return false;

    m_BVHNodesBuffer.reset( GPUBuffer::Create( 
          sizeof( m_BVHNodes )
        , sizeof( PackedBVHNode )
        , GPUResourceCreationFlags_CPUWriteable | GPUResourceCreationFlags_IsStructureBuffer ) );
    if ( !m_BVHNodesBuffer )
        return false;

    m_PointLightsBuffer.reset( GPUBuffer::Create( 
          sizeof( m_PointLights )
        , sizeof( PointLight )
        , GPUResourceCreationFlags_CPUWriteable | GPUResourceCreationFlags_IsStructureBuffer ) );
    if ( !m_PointLightsBuffer )
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

void Scene::ResetScene()
{
    m_Vertices[ 0 ].position = XMFLOAT3( -1.0f, 0.0f, 1.0f );
    m_Vertices[ 0 ].normal = XMFLOAT3( 0.0f, 1.0f, 0.0f );
    m_Vertices[ 0 ].tangent = XMFLOAT3( 1.0f, 0.0f, 0.0f );
    m_Vertices[ 1 ].position = XMFLOAT3( 1.0f, 0.0f, 1.0f );
    m_Vertices[ 1 ].normal = XMFLOAT3( 0.0f, 1.0f, 0.0f );
    m_Vertices[ 1 ].tangent = XMFLOAT3( 1.0f, 0.0f, 0.0f );
    m_Vertices[ 2 ].position = XMFLOAT3( 1.0f, 0.0f, -1.0f );
    m_Vertices[ 2 ].normal = XMFLOAT3( 0.0f, 1.0f, 0.0f );
    m_Vertices[ 2 ].tangent = XMFLOAT3( 1.0f, 0.0f, 0.0f );
    m_Vertices[ 3 ].position = XMFLOAT3( -1.0f, 0.0f, -1.0f );
    m_Vertices[ 3 ].normal = XMFLOAT3( 0.0f, 1.0f, 0.0f );
    m_Vertices[ 3 ].tangent = XMFLOAT3( 1.0f, 0.0f, 0.0f );

    m_Vertices[ 4 ].position = XMFLOAT3( -1.0f, 0.0f, 1.0f );
    m_Vertices[ 4 ].normal = XMFLOAT3( 0.0f, 0.0f, -1.0f );
    m_Vertices[ 4 ].tangent = XMFLOAT3( 1.0f, 0.0f, 0.0f );
    m_Vertices[ 5 ].position = XMFLOAT3( 1.0f, 0.0f, 1.0f );
    m_Vertices[ 5 ].normal = XMFLOAT3( 0.0f, 0.0f, -1.0f );
    m_Vertices[ 5 ].tangent = XMFLOAT3( 1.0f, 0.0f, 0.0f );
    m_Vertices[ 6 ].position = XMFLOAT3( -1.0f, 2.0f, 1.0f );
    m_Vertices[ 6 ].normal = XMFLOAT3( 0.0f, 0.0f, -1.0f );
    m_Vertices[ 6 ].tangent = XMFLOAT3( 1.0f, 0.0f, 0.0f );
    m_Vertices[ 7 ].position = XMFLOAT3( 1.0f, 2.0f, 1.0f );
    m_Vertices[ 7 ].normal = XMFLOAT3( 0.0f, 0.0f, -1.0f );
    m_Vertices[ 7 ].tangent = XMFLOAT3( 1.0f, 0.0f, 0.0f );

    m_Vertices[ 8 ].position = XMFLOAT3( -1.0f, 2.0f, 1.0f );
    m_Vertices[ 8 ].normal = XMFLOAT3( 0.0f, -1.0f, 0.0f );
    m_Vertices[ 8 ].tangent = XMFLOAT3( 1.0f, 0.0f, 0.0f );
    m_Vertices[ 9 ].position = XMFLOAT3( 1.0f, 2.0f, 1.0f );
    m_Vertices[ 9 ].normal = XMFLOAT3( 0.0f, -1.0f, 0.0f );
    m_Vertices[ 9 ].tangent = XMFLOAT3( 1.0f, 0.0f, 0.0f );
    m_Vertices[ 10 ].position = XMFLOAT3( 1.0f, 2.0f, -1.0f );
    m_Vertices[ 10 ].normal = XMFLOAT3( 0.0f, -1.0f, 0.0f );
    m_Vertices[ 10 ].tangent = XMFLOAT3( 1.0f, 0.0f, 0.0f );
    m_Vertices[ 11 ].position = XMFLOAT3( -1.0f, 2.0f, -1.0f );
    m_Vertices[ 11 ].normal = XMFLOAT3( 0.0f, -1.0f, 0.0f );
    m_Vertices[ 11 ].tangent = XMFLOAT3( 1.0f, 0.0f, 0.0f );

    m_Vertices[ 12 ].position = XMFLOAT3( -1.0f, 0.0f, 1.0f );
    m_Vertices[ 12 ].normal = XMFLOAT3( 1.0f, 0.0f, 0.0f );
    m_Vertices[ 12 ].tangent = XMFLOAT3( 0.0f, 1.0f, 0.0f );
    m_Vertices[ 13 ].position = XMFLOAT3( -1.0f, 0.0f, -1.0f );
    m_Vertices[ 13 ].normal = XMFLOAT3( 1.0f, 0.0f, 0.0f );
    m_Vertices[ 13 ].tangent = XMFLOAT3( 0.0f, 1.0f, 0.0f );
    m_Vertices[ 14 ].position = XMFLOAT3( -1.0f, 2.0f, -1.0f );
    m_Vertices[ 14 ].normal = XMFLOAT3( 1.0f, 0.0f, 0.0f );
    m_Vertices[ 14 ].tangent = XMFLOAT3( 0.0f, 1.0f, 0.0f );
    m_Vertices[ 15 ].position = XMFLOAT3( -1.0f, 2.0f, 1.0f );
    m_Vertices[ 15 ].normal = XMFLOAT3( 1.0f, 0.0f, 0.0f );
    m_Vertices[ 15 ].tangent = XMFLOAT3( 0.0f, 1.0f, 0.0f );

    m_Vertices[ 16 ].position = XMFLOAT3( 1.0f, 0.0f, 1.0f );
    m_Vertices[ 16 ].normal = XMFLOAT3( -1.0f, 0.0f, 0.0f );
    m_Vertices[ 16 ].tangent = XMFLOAT3( 0.0f, 1.0f, 0.0f );
    m_Vertices[ 17 ].position = XMFLOAT3( 1.0f, 0.0f, -1.0f );
    m_Vertices[ 17 ].normal = XMFLOAT3( -1.0f, 0.0f, 0.0f );
    m_Vertices[ 17 ].tangent = XMFLOAT3( 0.0f, 1.0f, 0.0f );
    m_Vertices[ 18 ].position = XMFLOAT3( 1.0f, 2.0f, -1.0f );
    m_Vertices[ 18 ].normal = XMFLOAT3( -1.0f, 0.0f, 0.0f );
    m_Vertices[ 18 ].tangent = XMFLOAT3( 0.0f, 1.0f, 0.0f );
    m_Vertices[ 19 ].position = XMFLOAT3( 1.0f, 2.0f, 1.0f );
    m_Vertices[ 19 ].normal = XMFLOAT3( -1.0f, 0.0f, 0.0f );
    m_Vertices[ 19 ].tangent = XMFLOAT3( 0.0f, 1.0f, 0.0f );

    m_Triangles[ 0 ] = 0;
    m_Triangles[ 1 ] = 1;
    m_Triangles[ 2 ] = 2;

    m_Triangles[ 3 ] = 0;
    m_Triangles[ 4 ] = 2;
    m_Triangles[ 5 ] = 3;

    m_Triangles[ 6 ] = 5;
    m_Triangles[ 7 ] = 4;
    m_Triangles[ 8 ] = 6;

    m_Triangles[ 9 ] = 5;
    m_Triangles[ 10 ] = 6;
    m_Triangles[ 11 ] = 7;

    m_Triangles[ 12 ] = 9;
    m_Triangles[ 13 ] = 8;
    m_Triangles[ 14 ] = 10;

    m_Triangles[ 15 ] = 10;
    m_Triangles[ 16 ] = 8;
    m_Triangles[ 17 ] = 11;

    m_Triangles[ 18 ] = 12;
    m_Triangles[ 19 ] = 13;
    m_Triangles[ 20 ] = 15;

    m_Triangles[ 21 ] = 13;
    m_Triangles[ 22 ] = 14;
    m_Triangles[ 23 ] = 15;

    m_Triangles[ 24 ] = 17;
    m_Triangles[ 25 ] = 16;
    m_Triangles[ 26 ] = 19;

    m_Triangles[ 27 ] = 19;
    m_Triangles[ 28 ] = 18;
    m_Triangles[ 29 ] = 17;

    uint32_t* reorderTriangles = new uint32_t[ kMaxVertexIndexCount ];
    std::vector<UnpackedBVHNode> bvhNodes;
    BuildBVH( m_Vertices, m_Triangles, reorderTriangles, 10, &bvhNodes );
    memcpy( m_Triangles, reorderTriangles, sizeof( m_Triangles ) );
    delete[] reorderTriangles;

    assert( bvhNodes.size() <= kMaxBVHNodeCount );
    PackBVH( bvhNodes.data(), uint32_t( bvhNodes.size() ), m_BVHNodes );

    m_PointLights[ 0 ].position = XMFLOAT3( 4.0f, 9.0f, -5.0f );
    m_PointLights[ 0 ].color = XMFLOAT3( 200.0f, 200.0f, 200.0f );

    m_RayTracingConstants.maxBounceCount = 3;
    m_RayTracingConstants.primitiveCount = 10;
    m_RayTracingConstants.pointLightCount = 1;
    m_RayTracingConstants.filmSize = XMFLOAT2( 0.05333f, 0.03f );
    m_RayTracingConstants.filmDistance = 0.04f;
    m_RayTracingConstants.cameraTransform =
    { 1.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 1.0f, 0.0f,
      0.0f, 0.0f, 0.0f, 1.0f };
    m_RayTracingConstants.background = { 0.0f, 0.0f, 0.0f, 0.f };

    m_IsFilmDirty = true;
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

    if ( void* address = m_VerticesBuffer->Map() )
    {
        memcpy( address, m_Vertices, sizeof( m_Vertices ) );
        m_VerticesBuffer->Unmap();
    }
    else
    {
        return false;
    }

    if ( void* address = m_TrianglesBuffer->Map() )
    {
        memcpy( address, m_Triangles, sizeof( m_Triangles ) );
        m_TrianglesBuffer->Unmap();
    }
    else
    {
        return false;
    }

    if ( void* address = m_BVHNodesBuffer->Map() )
    {
        memcpy( address, m_BVHNodes, sizeof( m_BVHNodes ) );
        m_BVHNodesBuffer->Unmap();
    }
    else
    {
        return false;
    }

    if ( void* address = m_PointLightsBuffer->Map() )
    {
        memcpy( address, m_PointLights, sizeof( m_PointLights ) );
        m_PointLightsBuffer->Unmap();
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




