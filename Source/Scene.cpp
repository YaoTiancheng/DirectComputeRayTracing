#include "stdafx.h"
#include "Scene.h"
#include "D3D11RenderSystem.h"
#include "DDSTextureLoader.h"

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

bool Scene::Init( uint32_t resolutionWidth, uint32_t resolutionHeight )
{
    ID3D11Device* device = GetDevice();

    D3D11_TEXTURE2D_DESC textureDesc;
    ZeroMemory( &textureDesc, sizeof( D3D11_TEXTURE2D_DESC ) );
    textureDesc.Width = resolutionWidth;
    textureDesc.Height = resolutionHeight;
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Usage = D3D11_USAGE_DEFAULT;
    textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET;
    HRESULT hr = device->CreateTexture2D( &textureDesc, nullptr, &m_FilmTexture );
    if ( FAILED( hr ) )
        return false;

    hr = device->CreateUnorderedAccessView( m_FilmTexture.Get(), nullptr, &m_FilmTextureUAV );
    if ( FAILED( hr ) )
        return false;

    hr = device->CreateShaderResourceView( m_FilmTexture.Get(), nullptr, &m_FilmTextureSRV );
    if ( FAILED( hr ) )
        return false;

    hr = CreateDDSTextureFromFile( device, L"BuiltinResources\\CookTorranceComp_E.DDS", ( ID3D11Resource** ) m_CookTorranceCompETexture.ReleaseAndGetAddressOf(), &m_CookTorranceCompETextureSRV );
    if ( FAILED( hr ) )
        return false;

    hr = CreateDDSTextureFromFile( device, L"BuiltinResources\\CookTorranceComp_E_Avg.DDS", ( ID3D11Resource** ) m_CookTorranceCompEAvgTexture.ReleaseAndGetAddressOf(), &m_CookTorranceCompEAvgTextureSRV );
    if ( FAILED( hr ) )
        return false;

    hr = CreateDDSTextureFromFile( device, L"BuiltinResources\\CookTorranceComp_InvCDF.DDS", ( ID3D11Resource** ) m_CookTorranceCompInvCDFTexture.ReleaseAndGetAddressOf(), &m_CookTorranceCompInvCDFTextureSRV );
    if ( FAILED( hr ) )
        return false;

    hr = CreateDDSTextureFromFile( device, L"BuiltinResources\\CookTorranceComp_PdfScale.DDS", ( ID3D11Resource** ) m_CookTorranceCompPdfScaleTexture.ReleaseAndGetAddressOf(), &m_CookTorranceCompPdfScaleTextureSRV );
    if ( FAILED( hr ) )
        return false;

    hr = CreateDDSTextureFromFile( device, L"BuiltinResources\\CookTorranceComp_EFresnel.DDS", ( ID3D11Resource** ) m_CookTorranceCompEFresnelTexture .ReleaseAndGetAddressOf(), &m_CookTorranceCompEFresnelTextureSRV );
    if ( FAILED( hr ) )
        return false;

    ID3DBlob* shaderBlob = CompileFromFile( L"Shaders\\RayTracing.hlsl", "main", "cs_5_0" );
    if ( !shaderBlob )
        return false;

    m_RayTracingComputeShader.Attach( CreateComputeShader( shaderBlob ) );
    shaderBlob->Release();
    if ( !m_RayTracingComputeShader )
        return false;

    D3D11_BUFFER_DESC bufferDesc;
    ZeroMemory( &bufferDesc, sizeof( D3D11_BUFFER_DESC ) );
    bufferDesc.ByteWidth = sizeof( RayTracingConstants );
    bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bufferDesc.StructureByteStride = sizeof( RayTracingConstants );
    bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    hr = device->CreateBuffer( &bufferDesc, nullptr, &m_RayTracingConstantsBuffer );
    if ( FAILED( hr ) )
        return false;

    bufferDesc.ByteWidth = kMaxSamplesCount * sizeof( float );
    bufferDesc.StructureByteStride = sizeof( float );
    hr = device->CreateBuffer( &bufferDesc, nullptr, &m_SamplesBuffer );
    if ( FAILED( hr ) )
        return false;

    bufferDesc.ByteWidth = sizeof( m_Vertices );
    bufferDesc.StructureByteStride = sizeof( Vertex );
    hr = device->CreateBuffer( &bufferDesc, nullptr, &m_VerticesBuffer );
    if ( FAILED( hr ) )
        return false;

    bufferDesc.ByteWidth = sizeof( m_Triangles );
    bufferDesc.StructureByteStride = sizeof( uint32_t );
    hr = device->CreateBuffer( &bufferDesc, nullptr, &m_TrianglesBuffer );
    if ( FAILED( hr ) )
        return false;

    bufferDesc.ByteWidth = sizeof( m_PointLights );
    bufferDesc.StructureByteStride = sizeof( PointLight );
    hr = device->CreateBuffer( &bufferDesc, nullptr, &m_PointLightBuffer );
    if ( FAILED( hr ) )
        return false;

    bufferDesc.ByteWidth = sizeof( CookTorranceCompTextureConstants );
    bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
    bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bufferDesc.CPUAccessFlags = 0;
    bufferDesc.MiscFlags = 0;
    bufferDesc.StructureByteStride = 0;

    // Fill in the subresource data.
    CookTorranceCompTextureConstants cooktorranceCompTextureConstants;
    cooktorranceCompTextureConstants.compETextureSize           = XMFLOAT4( 32.0f, 32.0f, 1.0f / 32.0f, 1.0f / 32.0f );
    cooktorranceCompTextureConstants.compEAvgTextureSize        = XMFLOAT4( 32.0f, 1.0f, 1.0f / 32.0f, 1.0f );
    cooktorranceCompTextureConstants.compInvCDFTextureSize      = XMFLOAT4( 32.0f, 32.0f, 1.0f / 32.0f, 1.0f / 32.0f );
    cooktorranceCompTextureConstants.compPdfScaleTextureSize    = XMFLOAT4( 32.0f, 1.0f, 1.0f / 32.0f, 1.0f );
    cooktorranceCompTextureConstants.compEFresnelTextureSize    = XMFLOAT4( 32.0f, 16.0f, 16.0f, 0.0f );
    cooktorranceCompTextureConstants.compEFresnelTextureSizeRcp = XMFLOAT4( 1.0f / 32.0f, 1.0f / 16.0f, 1.0f / 16.0f, 0.0f );
    D3D11_SUBRESOURCE_DATA subresourceData;
    subresourceData.pSysMem = &cooktorranceCompTextureConstants;
    subresourceData.SysMemPitch = 0;
    subresourceData.SysMemSlicePitch = 0;
    hr = device->CreateBuffer( &bufferDesc, &subresourceData, &m_CookTorranceCompTextureConstantsBuffer );
    if ( FAILED( hr ) )
        return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc;
    SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
    SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    SRVDesc.Buffer.ElementOffset = 0;
    SRVDesc.Buffer.NumElements = 1;
    hr = device->CreateShaderResourceView( m_RayTracingConstantsBuffer.Get(), &SRVDesc, &m_RayTracingConstantsSRV );
    if ( FAILED( hr ) )
        return false;

    SRVDesc.Buffer.NumElements = kMaxSamplesCount;
    hr = device->CreateShaderResourceView( m_SamplesBuffer.Get(), &SRVDesc, &m_SamplesSRV );
    if ( FAILED( hr ) )
        return false;

    SRVDesc.Buffer.NumElements = kMaxVertexCount;
    hr = device->CreateShaderResourceView( m_VerticesBuffer.Get(), &SRVDesc, &m_VerticesSRV );
    if ( FAILED( hr ) )
        return false;

    SRVDesc.Buffer.NumElements = kMaxVertexIndexCount;
    hr = device->CreateShaderResourceView( m_TrianglesBuffer.Get(), &SRVDesc, &m_TrianglesSRV );
    if ( FAILED( hr ) )
        return false;

    SRVDesc.Buffer.NumElements = kMaxPointLightsCount;
    hr = device->CreateShaderResourceView( m_PointLightBuffer.Get(), &SRVDesc, &m_PointLightsSRV );
    if ( FAILED( hr ) )
        return false;

    shaderBlob = CompileFromFile( L"Shaders\\PostProcessings.hlsl", "ScreenQuadMainVS", "vs_5_0" );
    if ( !shaderBlob )
        return false;

    m_ScreenQuadVertexShader.Attach( CreateVertexShader( shaderBlob ) );
    hr = device->CreateInputLayout( kScreenQuadInputElementDesc, 1, shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), &m_ScreenQuadVertexInputLayout );
    shaderBlob->Release();
    if ( !m_ScreenQuadVertexShader || !m_ScreenQuadVertexInputLayout )
        return false;

    shaderBlob = CompileFromFile( L"Shaders\\PostProcessings.hlsl", "CopyMainPS", "ps_5_0" );
    if ( !shaderBlob )
        return false;

    m_CopyPixelShader.Attach( CreatePixelShader( shaderBlob ) );
    shaderBlob->Release();
    if ( !m_CopyPixelShader )
        return false;

    bufferDesc.ByteWidth = sizeof( kScreenQuadVertices );
    bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
    bufferDesc.CPUAccessFlags = 0;
    bufferDesc.StructureByteStride = sizeof( XMFLOAT4 );
    bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bufferDesc.MiscFlags = 0;
    subresourceData.SysMemPitch = 0;
    subresourceData.SysMemSlicePitch = 0;
    subresourceData.pSysMem = kScreenQuadVertices;
    hr = device->CreateBuffer( &bufferDesc, &subresourceData, &m_ScreenQuadVertexBuffer );
    if ( FAILED( hr ) )
        return false;

    ID3D11Texture2D *backBuffer = nullptr;
    GetSwapChain()->GetBuffer( 0, __uuidof( ID3D11Texture2D ), ( void** ) ( &backBuffer ) );
    hr = device->CreateRenderTargetView( backBuffer, nullptr, &m_DefaultRenderTargetView );
    backBuffer->Release();
    if ( FAILED( hr ) )
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
    hr = device->CreateSamplerState( &samplerDesc, &m_CopySamplerState );
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

    hr = device->CreateRenderTargetView( m_FilmTexture.Get(), nullptr, &m_FilmTextureRenderTargetView );
    if ( FAILED( hr ) )
        return false;

    m_DefaultViewport = { 0.0f, 0.0f, ( float ) resolutionWidth, ( float ) resolutionHeight, 0.0f, 1.0f };
    m_RayTracingConstants.samplesCount = kMaxSamplesCount;
    m_RayTracingConstants.resolution = { ( float ) resolutionWidth, ( float ) resolutionHeight };

    return true;
}

void Scene::ResetScene()
{
    m_Vertices[ 0 ].position = XMFLOAT4( -1.0f, 0.0f, 1.0f, 1.0f );
    m_Vertices[ 0 ].normal = XMFLOAT4( 0.0f, 1.0f, 0.0f, 0.0f );
    m_Vertices[ 0 ].tangent = XMFLOAT4( 1.0f, 0.0f, 0.0f, 0.0f );
    m_Vertices[ 1 ].position = XMFLOAT4( 1.0f, 0.0f, 1.0f, 1.0f );
    m_Vertices[ 1 ].normal = XMFLOAT4( 0.0f, 1.0f, 0.0f, 0.0f );
    m_Vertices[ 1 ].tangent = XMFLOAT4( 1.0f, 0.0f, 0.0f, 0.0f );
    m_Vertices[ 2 ].position = XMFLOAT4( 1.0f, 0.0f, -1.0f, 1.0f );
    m_Vertices[ 2 ].normal = XMFLOAT4( 0.0f, 1.0f, 0.0f, 0.0f );
    m_Vertices[ 2 ].tangent = XMFLOAT4( 1.0f, 0.0f, 0.0f, 0.0f );
    m_Vertices[ 3 ].position = XMFLOAT4( -1.0f, 0.0f, -1.0f, 1.0f );
    m_Vertices[ 3 ].normal = XMFLOAT4( 0.0f, 1.0f, 0.0f, 0.0f );
    m_Vertices[ 3 ].tangent = XMFLOAT4( 1.0f, 0.0f, 0.0f, 0.0f );

    m_Vertices[ 4 ].position = XMFLOAT4( -1.0f, 0.0f, 1.0f, 1.0f );
    m_Vertices[ 4 ].normal = XMFLOAT4( 0.0f, 0.0f, -1.0f, 0.0f );
    m_Vertices[ 4 ].tangent = XMFLOAT4( 1.0f, 0.0f, 0.0f, 0.0f );
    m_Vertices[ 5 ].position = XMFLOAT4( 1.0f, 0.0f, 1.0f, 1.0f );
    m_Vertices[ 5 ].normal = XMFLOAT4( 0.0f, 0.0f, -1.0f, 0.0f );
    m_Vertices[ 5 ].tangent = XMFLOAT4( 1.0f, 0.0f, 0.0f, 0.0f );
    m_Vertices[ 6 ].position = XMFLOAT4( -1.0f, 2.0f, 1.0f, 1.0f );
    m_Vertices[ 6 ].normal = XMFLOAT4( 0.0f, 0.0f, -1.0f, 0.0f );
    m_Vertices[ 6 ].tangent = XMFLOAT4( 1.0f, 0.0f, 0.0f, 0.0f );
    m_Vertices[ 7 ].position = XMFLOAT4( 1.0f, 2.0f, 1.0f, 1.0f );
    m_Vertices[ 7 ].normal = XMFLOAT4( 0.0f, 0.0f, -1.0f, 0.0f );
    m_Vertices[ 7 ].tangent = XMFLOAT4( 1.0f, 0.0f, 0.0f, 0.0f );

    m_Vertices[ 8 ].position = XMFLOAT4( -1.0f, 2.0f, 1.0f, 1.0f );
    m_Vertices[ 8 ].normal = XMFLOAT4( 0.0f, -1.0f, 0.0f, 0.0f );
    m_Vertices[ 8 ].tangent = XMFLOAT4( 1.0f, 0.0f, 0.0f, 0.0f );
    m_Vertices[ 9 ].position = XMFLOAT4( 1.0f, 2.0f, 1.0f, 1.0f );
    m_Vertices[ 9 ].normal = XMFLOAT4( 0.0f, -1.0f, 0.0f, 0.0f );
    m_Vertices[ 9 ].tangent = XMFLOAT4( 1.0f, 0.0f, 0.0f, 0.0f );
    m_Vertices[ 10 ].position = XMFLOAT4( 1.0f, 2.0f, -1.0f, 1.0f );
    m_Vertices[ 10 ].normal = XMFLOAT4( 0.0f, -1.0f, 0.0f, 0.0f );
    m_Vertices[ 10 ].tangent = XMFLOAT4( 1.0f, 0.0f, 0.0f, 0.0f );
    m_Vertices[ 11 ].position = XMFLOAT4( -1.0f, 2.0f, -1.0f, 1.0f );
    m_Vertices[ 11 ].normal = XMFLOAT4( 0.0f, -1.0f, 0.0f, 0.0f );
    m_Vertices[ 11 ].tangent = XMFLOAT4( 1.0f, 0.0f, 0.0f, 0.0f );

    m_Vertices[ 12 ].position = XMFLOAT4( -1.0f, 0.0f, 1.0f, 1.0f );
    m_Vertices[ 12 ].normal = XMFLOAT4( 1.0f, 0.0f, 0.0f, 0.0f );
    m_Vertices[ 12 ].tangent = XMFLOAT4( 0.0f, 1.0f, 0.0f, 0.0f );
    m_Vertices[ 13 ].position = XMFLOAT4( -1.0f, 0.0f, -1.0f, 1.0f );
    m_Vertices[ 13 ].normal = XMFLOAT4( 1.0f, 0.0f, 0.0f, 0.0f );
    m_Vertices[ 13 ].tangent = XMFLOAT4( 0.0f, 1.0f, 0.0f, 0.0f );
    m_Vertices[ 14 ].position = XMFLOAT4( -1.0f, 2.0f, -1.0f, 1.0f );
    m_Vertices[ 14 ].normal = XMFLOAT4( 1.0f, 0.0f, 0.0f, 0.0f );
    m_Vertices[ 14 ].tangent = XMFLOAT4( 0.0f, 1.0f, 0.0f, 0.0f );
    m_Vertices[ 15 ].position = XMFLOAT4( -1.0f, 2.0f, 1.0f, 1.0f );
    m_Vertices[ 15 ].normal = XMFLOAT4( 1.0f, 0.0f, 0.0f, 0.0f );
    m_Vertices[ 15 ].tangent = XMFLOAT4( 0.0f, 1.0f, 0.0f, 0.0f );

    m_Vertices[ 16 ].position = XMFLOAT4( 1.0f, 0.0f, 1.0f, 1.0f );
    m_Vertices[ 16 ].normal = XMFLOAT4( -1.0f, 0.0f, 0.0f, 0.0f );
    m_Vertices[ 16 ].tangent = XMFLOAT4( 0.0f, 1.0f, 0.0f, 0.0f );
    m_Vertices[ 17 ].position = XMFLOAT4( 1.0f, 0.0f, -1.0f, 1.0f );
    m_Vertices[ 17 ].normal = XMFLOAT4( -1.0f, 0.0f, 0.0f, 0.0f );
    m_Vertices[ 17 ].tangent = XMFLOAT4( 0.0f, 1.0f, 0.0f, 0.0f );
    m_Vertices[ 18 ].position = XMFLOAT4( 1.0f, 2.0f, -1.0f, 1.0f );
    m_Vertices[ 18 ].normal = XMFLOAT4( -1.0f, 0.0f, 0.0f, 0.0f );
    m_Vertices[ 18 ].tangent = XMFLOAT4( 0.0f, 1.0f, 0.0f, 0.0f );
    m_Vertices[ 19 ].position = XMFLOAT4( 1.0f, 2.0f, 1.0f, 1.0f );
    m_Vertices[ 19 ].normal = XMFLOAT4( -1.0f, 0.0f, 0.0f, 0.0f );
    m_Vertices[ 19 ].tangent = XMFLOAT4( 0.0f, 1.0f, 0.0f, 0.0f );

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

    m_PointLights[ 0 ].position = XMFLOAT4( 4.0f, 7.0f, -5.0f, 1.0f );
    m_PointLights[ 0 ].color = XMFLOAT4( 200.0f, 200.0f, 200.0f, 1.0f );

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
    m_RayTracingConstants.background = { 0.8f, 0.8f, 0.8f, 0.f };

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
    ID3D11Buffer* rawScreenQuadVertexBuffer = m_ScreenQuadVertexBuffer.Get();
    deviceContext->IASetVertexBuffers( 0, 1, &rawScreenQuadVertexBuffer, &vertexStride, &vertexOffset );
    deviceContext->IASetInputLayout( m_ScreenQuadVertexInputLayout.Get() );
    deviceContext->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );

    ID3D11RenderTargetView* rawDefaultRenderTargetView = m_DefaultRenderTargetView.Get();
    deviceContext->OMSetRenderTargets( 1, &rawDefaultRenderTargetView, nullptr );

    deviceContext->RSSetViewports( 1, &m_DefaultViewport );

    deviceContext->VSSetShader( m_ScreenQuadVertexShader.Get(), nullptr, 0 );
    deviceContext->PSSetShader( m_CopyPixelShader.Get(), nullptr, 0 );

    ID3D11SamplerState* rawCopySamplerState = m_CopySamplerState.Get();
    deviceContext->PSSetSamplers( 0, 1, &rawCopySamplerState );
    ID3D11ShaderResourceView* rawFilmTextureSRV = m_FilmTextureSRV.Get();
    deviceContext->PSSetShaderResources( 0, 1, &rawFilmTextureSRV );

    deviceContext->Draw( 6, 0 );

    rawFilmTextureSRV = nullptr;
    deviceContext->PSSetShaderResources( 0, 1, &rawFilmTextureSRV );
    rawCopySamplerState = nullptr;
    deviceContext->PSSetSamplers( 0, 1, &rawCopySamplerState );
}

bool Scene::UpdateResources()
{
    ID3D11DeviceContext* deviceContext = GetDeviceContext();

    D3D11_MAPPED_SUBRESOURCE mappedSubresource;

    ZeroMemory( &mappedSubresource, sizeof( D3D11_MAPPED_SUBRESOURCE ) );
    HRESULT hr = deviceContext->Map( m_RayTracingConstantsBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSubresource );
    if ( SUCCEEDED( hr ) )
    {
        memcpy( mappedSubresource.pData, &m_RayTracingConstants, sizeof( m_RayTracingConstants ) );
        deviceContext->Unmap( m_RayTracingConstantsBuffer.Get(), 0 );
    }
    else
    {
        return false;
    }

    ZeroMemory( &mappedSubresource, sizeof( D3D11_MAPPED_SUBRESOURCE ) );
    hr = deviceContext->Map( m_VerticesBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSubresource );
    if ( SUCCEEDED( hr ) )
    {
        memcpy( mappedSubresource.pData, m_Vertices, sizeof( Vertex ) * kMaxVertexCount );
        deviceContext->Unmap( m_VerticesBuffer.Get(), 0 );
    }
    else
    {
        return false;
    }

    ZeroMemory( &mappedSubresource, sizeof( D3D11_MAPPED_SUBRESOURCE ) );
    hr = deviceContext->Map( m_TrianglesBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSubresource );
    if ( SUCCEEDED( hr ) )
    {
        memcpy( mappedSubresource.pData, m_Triangles, sizeof( uint32_t ) * kMaxVertexIndexCount );
        deviceContext->Unmap( m_TrianglesBuffer.Get(), 0 );
    }
    else
    {
        return false;
    }

    ZeroMemory( &mappedSubresource, sizeof( D3D11_MAPPED_SUBRESOURCE ) );
    hr = deviceContext->Map( m_PointLightBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSubresource );
    if ( SUCCEEDED( hr ) )
    {
        memcpy( mappedSubresource.pData, m_PointLights, sizeof( PointLight ) * m_RayTracingConstants.pointLightCount );
        deviceContext->Unmap( m_PointLightBuffer.Get(), 0 );
    }
    else
    {
        return false;
    }

    ZeroMemory( &mappedSubresource, sizeof( D3D11_MAPPED_SUBRESOURCE ) );
    hr = deviceContext->Map( m_SamplesBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSubresource );
    if ( SUCCEEDED( hr ) )
    {
        float* samples = reinterpret_cast< float* >( mappedSubresource.pData );
        for ( int i = 0; i < kMaxSamplesCount; ++i )
        {
            samples[ i ] = m_UniformRealDistribution( m_MersenneURBG );
        }

        deviceContext->Unmap( m_SamplesBuffer.Get(), 0 );
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

    deviceContext->CSSetShader( m_RayTracingComputeShader.Get(), nullptr, 0 );

    ID3D11UnorderedAccessView* rawFilmTextureUAV = m_FilmTextureUAV.Get();
    deviceContext->CSSetUnorderedAccessViews( 0, 1, &rawFilmTextureUAV, nullptr );

    ID3D11SamplerState* rawUVClampSamplerState = m_UVClampSamplerState.Get();
    deviceContext->CSSetSamplers( 0, 1, &rawUVClampSamplerState );

    ID3D11ShaderResourceView* rawSRVs[] =
    {
          m_VerticesSRV.Get()
        , m_TrianglesSRV.Get()
        , m_PointLightsSRV.Get()
        , m_RayTracingConstantsSRV.Get()
        , m_SamplesSRV.Get()
        , m_CookTorranceCompETextureSRV.Get()
        , m_CookTorranceCompEAvgTextureSRV.Get()
        , m_CookTorranceCompInvCDFTextureSRV.Get()
        , m_CookTorranceCompPdfScaleTextureSRV.Get()
        , m_CookTorranceCompEFresnelTextureSRV.Get()
    };
    deviceContext->CSSetShaderResources( 0, 10, rawSRVs );

    ID3D11Buffer* rawConstantBuffers[] = { m_CookTorranceCompTextureConstantsBuffer.Get() };
    deviceContext->CSSetConstantBuffers( 0, 1, rawConstantBuffers );

    UINT threadGroupCountX = ( UINT ) ceil( m_RayTracingConstants.resolution.x / 32.0f );
    UINT threadGroupCountY = ( UINT ) ceil( m_RayTracingConstants.resolution.y / 32.0f );
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
    deviceContext->ClearRenderTargetView( m_FilmTextureRenderTargetView.Get(), kClearColor );
}




