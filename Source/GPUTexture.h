#pragma once

#include "D3D12Resource.h"
#include "D3D12DescriptorHandle.h"

enum EGPUTextureBindFlag : uint32_t
{
    EGPUTextureBindFlag_RenderTarget = 0x1,
    EGPUTextureBindFlag_ShaderResource = 0x2,
    EGPUTextureBindFlag_UnorderedAccess = 0x4,
};

class GPUTexture : public CD3D12Resource
{
public:
    static GPUTexture* Create( uint32_t width, uint32_t height, DXGI_FORMAT format, uint32_t bindFlags, uint32_t arraySize = 1,
        D3D12_RESOURCE_STATES resourceStates = D3D12_RESOURCE_STATE_COMMON, const char* debugName = nullptr );

    static GPUTexture* CreateFromSwapChain( uint32_t index );

    static GPUTexture* CreateFromSwapChain( DXGI_FORMAT format, uint32_t index );

    static GPUTexture* CreateFromFile( const wchar_t* filename );

    ~GPUTexture();

    ID3D12Resource* GetTexture() const { return m_Texture; }

    D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const { return m_Texture->GetGPUVirtualAddress(); }

    CD3D12DescritorHandle GetSRV() const { return m_SRV; }

    CD3D12DescritorHandle GetUAV() const { return m_UAV; }

    CD3D12DescritorHandle GetRTV() const { return m_RTV; }

private:
    static GPUTexture* CreateFromSwapChainInternal( const D3D12_RENDER_TARGET_VIEW_DESC *desc, uint32_t index );

    ComPtr<ID3D12Resource> m_Texture;
    CD3D12DescritorHandle m_SRV;
    CD3D12DescritorHandle m_UAV;
    CD3D12DescritorHandle m_RTV;
};