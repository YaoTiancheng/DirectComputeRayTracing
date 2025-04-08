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
    static GPUTexture* Create( uint32_t width, uint32_t height, DXGI_FORMAT format, uint32_t bindFlags, const std::vector<D3D12_SUBRESOURCE_DATA>& initialData, 
        uint32_t arraySize = 1, D3D12_RESOURCE_STATES resourceStates = D3D12_RESOURCE_STATE_COMMON, const wchar_t* debugName = nullptr,
        DirectX::XMFLOAT4 clearColor = DirectX::XMFLOAT4( 0.f, 0.f, 0.f, 0.f ) );

    static GPUTexture* Create( uint32_t width, uint32_t height, DXGI_FORMAT format, uint32_t bindFlags,
        uint32_t arraySize = 1, D3D12_RESOURCE_STATES resourceStates = D3D12_RESOURCE_STATE_COMMON, const wchar_t* debugName = nullptr,
        DirectX::XMFLOAT4 clearColor = DirectX::XMFLOAT4( 0.f, 0.f, 0.f, 0.f ) );

    static GPUTexture* CreateFromSwapChain( uint32_t index );

    static GPUTexture* CreateFromSwapChain( DXGI_FORMAT format, uint32_t index );

    static GPUTexture* CreateFromFile( const wchar_t* filename );

    ~GPUTexture();

    ID3D12Resource* GetTexture() const { return m_Texture.Get(); }

    D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const { return m_Texture->GetGPUVirtualAddress(); }

    SD3D12DescriptorHandle GetSRV() const { return m_SRV; }

    SD3D12DescriptorHandle GetUAV() const { return m_UAV; }

    SD3D12DescriptorHandle GetRTV() const { return m_RTV; }

private:
    static GPUTexture* CreateFromSwapChainInternal( const D3D12_RENDER_TARGET_VIEW_DESC *desc, uint32_t index );

    ComPtr<ID3D12Resource> m_Texture;
    SD3D12DescriptorHandle m_SRV;
    SD3D12DescriptorHandle m_UAV;
    SD3D12DescriptorHandle m_RTV;
};