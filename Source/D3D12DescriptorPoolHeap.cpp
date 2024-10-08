#include "stdafx.h"
#include "D3D12DescriptorPoolHeap.h"
#include "D3D12Adapter.h"

bool CD3D12DescriptorPoolHeap::Create( D3D12_DESCRIPTOR_HEAP_TYPE heapType, uint32_t size )
{
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    heapDesc.NumDescriptors = size;
    heapDesc.Type = heapType;
    if ( FAILED( D3D12Adapter::GetDevice()->CreateDescriptorHeap( &heapDesc, IID_PPV_ARGS( m_Heap.ReleaseAndGetAddressOf() ) ) ) )
    {
        return false;
    }

    m_FreeEntries.clear();
    for ( uint32_t entry = 0; entry < size; ++entry )
    {
        m_FreeEntries.emplace_back( entry );
    }

    return true;
}

void CD3D12DescriptorPoolHeap::Destroy()
{
    m_Heap.Reset();
    m_FreeEntries.clear();
}

namespace
{
    bool AllocateDescriptorHandle( ID3D12DescriptorHeap* heap, std::list<uint32_t>* freeEntries, CD3D12DescritorHandle* handle, uint32_t descriptorSize )
    {
        if ( freeEntries->empty() )
        {
            return false;
        }

        const uint32_t freeEntry = freeEntries->front();
        freeEntries->pop_front();

        handle->InitOffseted( heap, freeEntry, descriptorSize );

        return true;
    }

    void FreeDescriptorHandle( ID3D12DescriptorHeap* heap, std::list<uint32_t>* freeEntries, const CD3D12DescritorHandle& handle, uint32_t descriptorSize )
    {
        const uint32_t entry = ( handle.CPU.ptr - heap->GetCPUDescriptorHandleForHeapStart().ptr ) / descriptorSize;
        assert( ( heap->GetGPUDescriptorHandleForHeapStart() + entry * descriptorSize ) == handle.GPU.ptr );
        freeEntries->emplace_front( entry );
    }
}

CD3D12DescritorHandle CD3D12DescriptorPoolHeap::Allocate( ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* desc )
{
    CD3D12DescritorHandle handle;
    if ( AllocateDescriptorHandle( m_Heap.Get(), &m_FreeEntries, &handle, D3D12Adapter::GetCBVSRVUAVDescriptorSize() ) )
    {
        D3D12Adapter::GetDevice()->CreateShaderResourceView( resource, desc, handle.CPU );
    }
    return handle;
}

CD3D12DescritorHandle CD3D12DescriptorPoolHeap::Allocate( ID3D12Resource* resource, const D3D12_UNORDERED_ACCESS_VIEW_DESC* desc )
{
    CD3D12DescritorHandle handle;
    if ( AllocateDescriptorHandle( m_Heap.Get(), &m_FreeEntries, &handle, D3D12Adapter::GetCBVSRVUAVDescriptorSize() ) )
    {
        D3D12Adapter::GetDevice()->CreateUnorderedAccessView( resource, nullptr, desc, handle.CPU );
    }
    return handle;
}

CD3D12DescritorHandle CD3D12DescriptorPoolHeap::Allocate( const D3D12_CONSTANT_BUFFER_VIEW_DESC* desc )
{
    CD3D12DescritorHandle handle;
    if ( AllocateDescriptorHandle( m_Heap.Get(), &m_FreeEntries, &handle, D3D12Adapter::GetCBVSRVUAVDescriptorSize() ) )
    {
        D3D12Adapter::GetDevice()->CreateConstantBufferView( desc, handle.CPU );
    }
    return handle;
}

CD3D12DescritorHandle CD3D12DescriptorPoolHeap::Allocate( ID3D12Resource* resource, const D3D12_RENDER_TARGET_VIEW_DESC* desc )
{
    CD3D12DescritorHandle handle;
    if ( AllocateDescriptorHandle( m_Heap.Get(), &m_FreeEntries, &handle, D3D12Adapter::GetRTVDescriptorSize() ) )
    {
        D3D12Adapter::GetDevice()->CreateRenderTargetView( resource, desc, handle.CPU );
    }
    return handle;
}

CD3D12DescritorHandle CD3D12DescriptorPoolHeap::Allocate( const D3D12_SAMPLER_DESC* desc )
{
    CD3D12DescritorHandle handle;
    if ( AllocateDescriptorHandle( m_Heap.Get(), &m_FreeEntries, &handle, D3D12Adapter::GetSamplerDescriptorSize() ) )
    {
        D3D12Adapter::GetDevice()->CreateSampler( desc, handle.CPU );
    }
    return handle;
}

void CD3D12DescriptorPoolHeap::FreeCBVSRVUAV( const CD3D12DescritorHandle& handle )
{
    FreeDescriptorHandle( m_Heap.Get(), &m_FreeEntries, handle, D3D12Adapter::GetCBVSRVUAVDescriptorSize() );
}

void CD3D12DescriptorPoolHeap::FreeRTV( const CD3D12DescritorHandle& handle )
{
    FreeDescriptorHandle( m_Heap.Get(), &m_FreeEntries, handle, D3D12Adapter::GetRTVDescriptorSize() );
}

void CD3D12DescriptorPoolHeap::FreeSampler( const CD3D12DescritorHandle& handle )
{
    FreeDescriptorHandle( m_Heap.Get(), &m_FreeEntries, handle, D3D12Adapter::GetSamplerDescriptorSize() );
}
