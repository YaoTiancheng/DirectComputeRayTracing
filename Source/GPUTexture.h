#pragma once

#include "GPUResourceCreationFlag.h"

class GPUTexture
{
public:
    static GPUTexture* Create( uint32_t width, uint32_t height, DXGI_FORMAT format, uint32_t flags );

    static GPUTexture* CreateFromSwapChain( DXGI_FORMAT format );

    static GPUTexture* CreateFromFile( const wchar_t* filename );

    ~GPUTexture();

    ID3D11ShaderResourceView*   GetSRV() const { return m_SRV; }

    ID3D11UnorderedAccessView*  GetUAV() const { return m_UAV; }

    ID3D11RenderTargetView*     GetRTV() const { return m_RTV; }

private:
    GPUTexture();

    ID3D11Resource*             m_Texture;
    ID3D11ShaderResourceView*   m_SRV;
    ID3D11UnorderedAccessView*  m_UAV;
    ID3D11RenderTargetView*     m_RTV;
};