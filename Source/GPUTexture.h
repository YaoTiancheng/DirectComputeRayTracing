#pragma once

#include "GPUResourceCreationFlag.h"
#include "D3D12DescriptorHandle.h"

class GPUTexture
{
public:
    static GPUTexture* Create( uint32_t width, uint32_t height, DXGI_FORMAT format, uint32_t flags, uint32_t arraySize = 1, const D3D11_SUBRESOURCE_DATA* initialData = nullptr, const char* debugName = nullptr );

    static GPUTexture* CreateFromSwapChain();

    static GPUTexture* CreateFromSwapChain( DXGI_FORMAT format );

    static GPUTexture* CreateFromFile( const wchar_t* filename );

    ~GPUTexture();

    ID3D12Resource* GetTexture() const { return m_Texture; }

    CD3D12DescritorHandle GetSRV() const { return m_SRV; }

    CD3D12DescritorHandle GetUAV() const { return m_UAV; }

    CD3D12DescritorHandle GetRTV() const { return m_RTV; }

private:
    GPUTexture();

    static GPUTexture* CreateFromSwapChainInternal( const D3D11_RENDER_TARGET_VIEW_DESC *desc );

    ID3D12Resource* m_Texture;
    CD3D12DescritorHandle m_SRV;
    CD3D12DescritorHandle m_UAV;
    CD3D12DescritorHandle m_RTV;
};