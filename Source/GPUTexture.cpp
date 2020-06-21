#include "stdafx.h"
#include "GPUTexture.h"
#include "D3D11RenderSystem.h"
#include "DDSTextureLoader.h"

GPUTexture* GPUTexture::Create( uint32_t width, uint32_t height, DXGI_FORMAT format, uint32_t flags )
{
    bool hasUAV         = ( flags & GPUResourceCreationFlags::GPUResourceCreationFlags_HasUAV ) != 0;
    bool isRenderTarget = ( flags & GPUResourceCreationFlags::GPUResourceCreationFlags_IsRenderTarget ) != 0;

    D3D11_TEXTURE2D_DESC desc;
    ZeroMemory( &desc, sizeof( desc ) );
    desc.Width              = width;
    desc.Height             = height;
    desc.MipLevels          = 1;
    desc.ArraySize          = 1;
    desc.Format             = format;
    desc.SampleDesc.Count   = 1;
    desc.Usage              = D3D11_USAGE_DEFAULT;
    uint32_t bindFlags = D3D11_BIND_SHADER_RESOURCE;
    if ( hasUAV )
        bindFlags |= D3D11_BIND_UNORDERED_ACCESS;
    if ( isRenderTarget )
        bindFlags |= D3D11_BIND_RENDER_TARGET;
    desc.BindFlags          = bindFlags;

    ID3D11Texture2D* texture = nullptr;
    HRESULT hr = GetDevice()->CreateTexture2D( &desc, nullptr, &texture );
    if ( FAILED( hr ) )
        return nullptr;

    ID3D11ShaderResourceView* SRV = nullptr;
    hr = GetDevice()->CreateShaderResourceView( texture, nullptr, &SRV );
    if ( FAILED( hr ) )
    {
        texture->Release();
        return nullptr;
    }

    ID3D11UnorderedAccessView* UAV = nullptr;
    if ( hasUAV )
    {
        hr = GetDevice()->CreateUnorderedAccessView( texture, nullptr, &UAV );
        if ( FAILED( hr ) )
        {
            SRV->Release();
            texture->Release();
            return nullptr;
        }
    }

    ID3D11RenderTargetView* RTV = nullptr;
    if ( isRenderTarget )
    {
        hr = GetDevice()->CreateRenderTargetView( texture, nullptr, &RTV );
        if ( FAILED( hr ) )
        {
            if ( UAV )
                UAV->Release();
            RTV->Release();
            SRV->Release();
            texture->Release();
            return nullptr;
        }
    }

    GPUTexture* gpuTexture = new GPUTexture();
    gpuTexture->m_Texture = texture;
    gpuTexture->m_SRV = SRV;
    gpuTexture->m_UAV = UAV;
    gpuTexture->m_RTV = RTV;

    return gpuTexture;
}

GPUTexture* GPUTexture::CreateFromSwapChain()
{
    ID3D11Texture2D* backBuffer = nullptr;
    HRESULT hr = GetSwapChain()->GetBuffer( 0, __uuidof( ID3D11Texture2D ), (void**)( &backBuffer ) );
    if ( FAILED( hr ) )
        return nullptr;

    ID3D11RenderTargetView* RTV = nullptr;
    hr = GetDevice()->CreateRenderTargetView( backBuffer, nullptr, &RTV );
    if ( FAILED( hr ) )
    {
        backBuffer->Release();
        return nullptr;
    }

    backBuffer->Release();

    GPUTexture* gpuTexture = new GPUTexture();
    gpuTexture->m_RTV = RTV;
    
    return gpuTexture;
}

GPUTexture* GPUTexture::CreateFromFile( const wchar_t* filename )
{
    ID3D11Resource* texture = nullptr;
    ID3D11ShaderResourceView* SRV = nullptr;
    HRESULT hr = DirectX::CreateDDSTextureFromFile( GetDevice(), filename, &texture, &SRV );
    if ( FAILED( hr ) )
        return nullptr;

    GPUTexture* gpuTexture = new GPUTexture();
    gpuTexture->m_Texture = texture;
    gpuTexture->m_SRV = SRV;

    return gpuTexture;
}

GPUTexture::GPUTexture()
    : m_Texture( nullptr )
    , m_SRV( nullptr )
    , m_UAV( nullptr )
    , m_RTV( nullptr )
{
}

GPUTexture::~GPUTexture()
{
    if ( m_SRV )
        m_SRV->Release();
    if ( m_UAV )
        m_UAV->Release();
    if ( m_RTV )
        m_RTV->Release();
    if ( m_Texture )
        m_Texture->Release();
}
