#include "stdafx.h"
#include "D3D12DescriptorPoolHeap.h"
#include "D3D12GPUDescriptorHeap.h"
#include "D3D12DescriptorUtil.h"
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

CD3D12DescritorHandle CD3D12DescriptorPoolHeap::Allocate( D3D12_DESCRIPTOR_HEAP_TYPE type )
{
    CD3D12DescritorHandle handle;
    AllocateDescriptorHandle( m_Heap.Get(), &m_FreeEntries, &handle, D3D12Adapter::GetDescriptorSize( type ) );
    return handle;
}

void CD3D12DescriptorPoolHeap::Free( const CD3D12DescritorHandle& handle, D3D12_DESCRIPTOR_HEAP_TYPE type )
{
    FreeDescriptorHandle( m_Heap.Get(), &m_FreeEntries, handle, D3D12Adapter::GetDescriptorSize( type ) );
}

bool CD3D12GPUDescriptorHeap::Create( D3D12_DESCRIPTOR_HEAP_TYPE heapType, uint32_t size )
{
    uint32_t backbufferCount = D3D12Adapter::GetBackbufferCount();
    m_Heaps.resize( backbufferCount );

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    heapDesc.NumDescriptors = size;
    heapDesc.Type = heapType;

    for ( uint32_t i = 0; i < backbufferCount; ++i )
    {
        if ( FAILED( D3D12Adapter::GetDevice()->CreateDescriptorHeap( &heapDesc, IID_PPV_ARGS( m_Heaps[ i ].ReleaseAndGetAddressOf() ) ) ) )
        {
            return false;
        }
    }

    m_Size = size;
    m_Top = 0;
}

void CD3D12GPUDescriptorHeap::Destroy()
{
    for ( auto& heap : m_Heaps )
    {
        heap.Reset();
    }

    m_Size = 0;
    m_Top = 0;
}

CD3D12DescritorHandle CD3D12GPUDescriptorHeap::Allocate( D3D12_DESCRIPTOR_HEAP_TYPE type )
{
    return AllocateRange( type, 1 );
}

CD3D12DescritorHandle CD3D12GPUDescriptorHeap::AllocateRange( D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t number )
{
    CD3D12DescritorHandle handle;
    if ( m_Top + number <= m_Size )
    {
        handle.InitOffseted( m_Heaps[ D3D12Adapter::GetBackbufferIndex() ].Get(), m_Top, D3D12Adapter::GetDescriptorSize( type ) );
        m_Top += number;
    }
    return handle;
}

using namespace D3D12Util;

CD3D12DescritorHandle SD3D12DescriptorTableLayout::AllocateAndCopyToGPUDescriptorHeap( CD3D12DescritorHandle* SRVs, uint32_t SRVCount, CD3D12DescritorHandle* UAVs, uint32_t UAVCount )
{
    CD3D12DescritorHandle descriptorTable = D3D12Adapter::GetGPUDescriptorHeap( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV )->AllocateRange( m_SRVCount + m_UAVCount );
    CD3D12DescritorHandle* src[] = { SRVs, UAVs };
    uint32_t offsets[] = { 0, m_SRVCount };
    uint32_t sizes[] = { SRVCount, UAVCount };
    assert( SRVCount <= m_SRVCount );
    assert( UAVCount <= m_UAVCount );
    CopyToDescriptorTable( descriptorTable, src, offsets, sizes, 2, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
    return descriptorTable;
}

CD3D12DescritorHandle SD3D12DescriptorTableLayout::AllocateAndCopyToGPUDescriptorHeap( CD3D12DescritorHandle* descriptors, uint32_t count )
{
    assert( count <= m_SRVCount + m_UAVCount );
    CD3D12DescritorHandle descriptorTable = D3D12Adapter::GetGPUDescriptorHeap( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV )->AllocateRange( m_SRVCount + m_UAVCount );
    CopyDescriptors( descriptorTable, descriptors, count, D3D12Adapter::GetDescriptorSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ) );
    return descriptorTable;
}

