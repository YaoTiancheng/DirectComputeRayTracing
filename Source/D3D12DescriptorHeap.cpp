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
    bool AllocateDescriptorHandle( ID3D12DescriptorHeap* heap, std::list<uint32_t>* freeEntries, SD3D12DescriptorHandle* handle, uint32_t descriptorSize )
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

    void FreeDescriptorHandle( ID3D12DescriptorHeap* heap, std::list<uint32_t>* freeEntries, const SD3D12DescriptorHandle& handle, uint32_t descriptorSize )
    {
        const uint32_t entry = (uint32_t)( ( handle.CPU.ptr - heap->GetCPUDescriptorHandleForHeapStart().ptr ) / descriptorSize );
        freeEntries->emplace_front( entry );
    }
}

SD3D12DescriptorHandle CD3D12DescriptorPoolHeap::Allocate( D3D12_DESCRIPTOR_HEAP_TYPE type )
{
    SD3D12DescriptorHandle handle;
    AllocateDescriptorHandle( m_Heap.Get(), &m_FreeEntries, &handle, D3D12Adapter::GetDescriptorSize( type ) );
    return handle;
}

void CD3D12DescriptorPoolHeap::Free( const SD3D12DescriptorHandle& handle, D3D12_DESCRIPTOR_HEAP_TYPE type )
{
    FreeDescriptorHandle( m_Heap.Get(), &m_FreeEntries, handle, D3D12Adapter::GetDescriptorSize( type ) );
}

bool CD3D12GPUDescriptorHeap::Create( D3D12_DESCRIPTOR_HEAP_TYPE heapType, uint32_t reserved, uint32_t size )
{
    uint32_t backbufferCount = D3D12Adapter::GetBackbufferCount();

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    heapDesc.NumDescriptors = reserved + size * backbufferCount;
    heapDesc.Type = heapType;
    
    if ( FAILED( D3D12Adapter::GetDevice()->CreateDescriptorHeap( &heapDesc, IID_PPV_ARGS( m_Heap.GetAddressOf() ) ) ) )
    {
        return false;
    }

    m_Size = size;
    m_Reserved = reserved;
    m_Top = 0;

    return true;
}

void CD3D12GPUDescriptorHeap::Destroy()
{
    m_Heap.Reset();

    m_Size = 0;
    m_Reserved = 0;
    m_Top = 0;
}

SD3D12GPUDescriptorHeapHandle CD3D12GPUDescriptorHeap::GetReserved( uint32_t index ) const
{
    assert( index < m_Reserved );
    SD3D12GPUDescriptorHeapHandle handle;
    const uint32_t descriptorSize = D3D12Adapter::GetDescriptorSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
    handle.m_CPU = CD3DX12_CPU_DESCRIPTOR_HANDLE( m_Heap->GetCPUDescriptorHandleForHeapStart(), index, descriptorSize );
    handle.m_GPU = CD3DX12_GPU_DESCRIPTOR_HANDLE( m_Heap->GetGPUDescriptorHandleForHeapStart(), index, descriptorSize );
    return handle;
}

SD3D12GPUDescriptorHeapHandle CD3D12GPUDescriptorHeap::Allocate( D3D12_DESCRIPTOR_HEAP_TYPE type )
{
    return AllocateRange( type, 1 );
}

SD3D12GPUDescriptorHeapHandle CD3D12GPUDescriptorHeap::AllocateRange( D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t number )
{
    SD3D12GPUDescriptorHeapHandle handle = SD3D12GPUDescriptorHeapHandle::GetNull();
    if ( m_Top + number <= m_Size )
    {
        uint32_t offset = D3D12Adapter::GetBackbufferIndex() * m_Size + m_Top + m_Reserved;
        handle.m_CPU = CD3DX12_CPU_DESCRIPTOR_HANDLE( m_Heap->GetCPUDescriptorHandleForHeapStart(), offset, D3D12Adapter::GetDescriptorSize( type ) );
        handle.m_GPU = CD3DX12_GPU_DESCRIPTOR_HANDLE( m_Heap->GetGPUDescriptorHandleForHeapStart(), offset, D3D12Adapter::GetDescriptorSize( type ) );
        m_Top += number;
    }
    return handle;
}

using namespace D3D12Util;

static void FillDescriptors( D3D12_CPU_DESCRIPTOR_HANDLE descriptorTable, uint32_t offset, uint32_t count, D3D12_CPU_DESCRIPTOR_HANDLE descriptor )
{
    const uint32_t descriptorSize = D3D12Adapter::GetDescriptorSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
    CD3DX12_CPU_DESCRIPTOR_HANDLE dstDescriptor( descriptorTable, offset, descriptorSize );
    for ( uint32_t i = 0; i < count; ++i )
    {
        D3D12Adapter::GetDevice()->CopyDescriptorsSimple( 1, dstDescriptor, descriptor, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
        dstDescriptor.Offset( 1, descriptorSize );
    }
}

D3D12_GPU_DESCRIPTOR_HANDLE SD3D12DescriptorTableLayout::AllocateAndCopyToGPUDescriptorHeap( const SD3D12DescriptorHandle* SRVs, uint32_t SRVCount, const SD3D12DescriptorHandle* UAVs, uint32_t UAVCount )
{
    SD3D12GPUDescriptorHeapHandle descriptorTable = D3D12Adapter::GetGPUDescriptorHeap()->AllocateRange( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_SRVCount + m_UAVCount );

    const SD3D12DescriptorHandle* src[] = { SRVs, UAVs };
    uint32_t offsets[] = { 0, m_SRVCount };
    uint32_t sizes[] = { SRVCount, UAVCount };
    assert( SRVCount <= m_SRVCount );
    assert( UAVCount <= m_UAVCount );
    CopyToDescriptorTable( descriptorTable.m_CPU, src, offsets, sizes, 2, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

    // Fill remaining entries in the descriptor table with null descriptors even if they are not accessed by shader.
    // Otherwise the debug layer will report uninitialized descriptor error.
    if ( m_SRVCount > SRVCount )
    {
        FillDescriptors( descriptorTable.m_CPU, SRVCount, m_SRVCount - SRVCount, D3D12Adapter::GetNullBufferSRV() );
    }
    if ( m_UAVCount > UAVCount )
    {
        FillDescriptors( descriptorTable.m_CPU, m_SRVCount + UAVCount, m_UAVCount - UAVCount, D3D12Adapter::GetNullBufferUAV() );
    }

    return descriptorTable.m_GPU;
}

D3D12_GPU_DESCRIPTOR_HANDLE SD3D12DescriptorTableLayout::AllocateAndCopyToGPUDescriptorHeap( const SD3D12DescriptorHandle* descriptors, uint32_t count )
{
    assert( count <= m_SRVCount + m_UAVCount );
    SD3D12GPUDescriptorHeapHandle descriptorTable = D3D12Adapter::GetGPUDescriptorHeap()->AllocateRange( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_SRVCount + m_UAVCount );
    CopyDescriptors( descriptorTable.m_CPU, descriptors, count, D3D12Adapter::GetDescriptorSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
    return descriptorTable.m_GPU;
}

