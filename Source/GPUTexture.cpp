#include "stdafx.h"
#include "GPUTexture.h"
#include "D3D12Adapter.h"
#include "D3D12DescriptorPoolHeap.h"
#include "DDSTextureLoader12/DDSTextureLoader12.h"

GPUTexture* GPUTexture::Create( uint32_t width, uint32_t height, DXGI_FORMAT format, uint32_t bindFlags, uint32_t arraySize, D3D12_RESOURCE_STATES resourceStates, const char* debugName )
{
    const bool hasUAV = ( bindFlags & EGPUTextureBindFlag_UnorderedAccess ) != 0;
    const bool isRenderTarget = ( bindFlags & EGPUTextureBindFlag_RenderTarget ) != 0;

    ComPtr<ID3D12Resource> texture;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = arraySize;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    if ( hasUAV )
    {
        desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }
    if ( isRenderTarget )
    {
        desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    }

    if ( FAILED( D3D12Adapter::GetDevice()->CreateCommittedResource( CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_DEFAULT ), D3D12_HEAP_FLAG_NONE, &desc,
        resourceStates, nullptr, IID_PPV_ARGS( texture.GetAddressOf() ) ) ) )
    {
        return nullptr;
    }

    CD3D12DescriptorPoolHeap* descriptorHeap = D3D12Adapter::GetDescriptorPoolHeap( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

    SD3D12DescriptorHandle SRV = descriptorHeap->Allocate( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
    if ( !SRV )
    {
        return nullptr;
    }
    D3D12Adapter::GetDevice()->CreateShaderResourceView( texture.get(), nullptr, SRV.CPU );

    SD3D12DescriptorHandle UAV;
    if ( hasUAV )
    {
        UAV = descriptorHeap->Allocate( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
        if ( !UAV )
        {
            descriptorHeap->Free( SRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
            return nullptr;
        }
        D3D12Adapter::GetDevice()->CreateUnorderedAccessView( texture.get(), nullptr, nullptr, UAV.CPU );
    }

    SD3D12DescriptorHandle RTV;
    if ( isRenderTarget )
    {
        RTV = descriptorHeap->Allocate( D3D12_DESCRIPTOR_HEAP_TYPE_RTV );
        if ( !RTV )
        {
            descriptorHeap->Free( SRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
            if ( UAV )
            {
                descriptorHeap->Free( UAV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
            }
        }
        D3D12Adapter::GetDevice()->CreateRenderTargetView( texture.get(), nullptr, RTV.CPU );
    }

    if ( debugName )
    {
        texture->SetPrivateData( WKPDID_D3DDebugObjectName, (UINT)strlen( debugName ), debugName );
    }

    GPUTexture* gpuTexture = new GPUTexture();
    gpuTexture->m_Texture = texture;
    gpuTexture->m_SRV = SRV;
    gpuTexture->m_UAV = UAV;
    gpuTexture->m_RTV = RTV;

    return gpuTexture;
}

GPUTexture* GPUTexture::CreateFromSwapChain( uint32_t index )
{
    return CreateFromSwapChainInternal( nullptr, index );
}

GPUTexture* GPUTexture::CreateFromSwapChain( DXGI_FORMAT format, uint32_t index )
{
    D3D12_RENDER_TARGET_VIEW_DESC desc = {};
    desc.Format = format;
    desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    return CreateFromSwapChainInternal( &desc, index );
}

GPUTexture* GPUTexture::CreateFromSwapChainInternal( const D3D12_RENDER_TARGET_VIEW_DESC* desc, uint32_t index )
{
    ComPtr<ID3D12Resource> backBuffer;
    HRESULT hr = GetSwapChain()->GetBuffer( index, IID_PPV_ARGS( backBuffer.GetAddressOf() ) );
    if ( FAILED( hr ) )
    {
        return nullptr;
    }

    CD3D12DescriptorPoolHeap* descriptorHeap = D3D12Adapter::GetDescriptorPoolHeap( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
    SD3D12DescriptorHandle RTV = descriptorHeap->Allocate( D3D12_DESCRIPTOR_HEAP_TYPE_RTV );
    if ( !RTV.IsValid() )
    {
        return nullptr;
    }

    D3D12Adapter::GetDevice()->CreateRenderTargetView( backBuffer.Get(), desc, RTV.CPU );

    GPUTexture* gpuTexture = new GPUTexture();
    gpuTexture->m_Texture = backBuffer;
    gpuTexture->m_RTV = RTV;
    
    return gpuTexture;
}

GPUTexture* GPUTexture::CreateFromFile( const wchar_t* filename )
{
    std::unique_ptr<uint8_t[]> ddsData;
    std::vector<D3D12_SUBRESOURCE_DATA> subresources;
    bool isCubemap = false;
    ID3D12Resource* D3DTexture = nullptr;
    HRESULT hr = DirectX::LoadDDSTextureFromFile( D3D12Adapter::GetDevice(), filename, &D3DTexture, ddsData, subresources, 0, nullptr, &isCubemap );
    if ( FAILED( hr ) )
    {
        return nullptr;
    }

    CD3D12ComPtr<ID3D12Resource> texture( D3DTexture );

    const UINT64 textureByteSize = GetRequiredIntermediateSize( texture.Get(), 0, (UINT)subresources.size() );
    const CD3DX12_HEAP_PROPERTIES heapProperties( D3D12_HEAP_TYPE_UPLOAD );
    const CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer( textureByteSize );
    hr = D3D12Adapter::GetDevice()->CreateCommittedResource( &heapProperties, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS( &D3DTexture ) );
    if ( FAILED( hr ) )
    {
        return nullptr;
    }

    CD3D12ComPtr<ID3D12Resource> intermediate( D3DTexture );

    ID3D12GraphicsCommandList* commandList = D3D12Adapter::GetCommandList();

    UpdateSubresources( commandList, texture.Get(), intermediate.Get(), 0, 0, (UINT)subresources.size(), subresources.data() );

    D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition( texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE );
    commandList->ResourceBarrier( 1, &barrier );

    // Create SRV
    CD3D12DescriptorPoolHeap* descriptorHeap = D3D12Adapter::GetDescriptorPoolHeap( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
    SD3D12DescriptorHandle SRV = descriptorHeap->Allocate( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
    if ( !SRV )
    {
        return nullptr;
    }
    D3D12Adapter::GetDevice()->CreateShaderResourceView( texture.Get(), nullptr, SRV.CPU );

    GPUTexture* gpuTexture = new GPUTexture();
    gpuTexture->m_Texture.Reset( texture.Get() );
    gpuTexture->m_SRV = SRV;
    return gpuTexture;
}

GPUTexture::~GPUTexture()
{
    CD3D12DescriptorPoolHeap* descriptorHeap = D3D12Adapter::GetDescriptorPoolHeap( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
    if ( m_SRV )
    { 
        descriptorHeap->Free( m_SRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
    }
    if ( m_UAV )
    { 
        descriptorHeap->Free( m_UAV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
    }
    if ( m_RTV )
    { 
        descriptorHeap->Free( m_RTV, D3D12_DESCRIPTOR_HEAP_TYPE_RTV );
    }
}
