#pragma once

#include "D3D12DescriptorHandle.h"

class CD3D12DescriptorPoolHeap
{
public:
    bool Create( D3D12_DESCRIPTOR_HEAP_TYPE heapType, uint32_t size );

    void Destroy();

    bool CanAllocate() const { return !m_FreeEntries.empty(); }

    SD3D12DescriptorHandle Allocate( D3D12_DESCRIPTOR_HEAP_TYPE type );

    void Free( const SD3D12DescriptorHandle& handle, D3D12_DESCRIPTOR_HEAP_TYPE type );

private:
    ComPtr<ID3D12DescriptorHeap> m_Heap;
    std::list<uint32_t> m_FreeEntries; // TODO: merge continuous entries
};

template <D3D12_DESCRIPTOR_HEAP_TYPE HeapType>
class TD3D12DescriptorPoolHeapRef
{
public:
    TD3D12DescriptorPoolHeapRef() : m_Heap( nullptr ) {}

    TD3D12DescriptorPoolHeapRef( CD3D12DescriptorPoolHeap* heap ) : m_Heap( heap ) {}

    bool CanAllocate() const { return m_Heap->CanAllocate(); }

    SD3D12DescriptorHandle Allocate() { return m_Heap->Allocate( HeapType ); }

    void Free( const SD3D12DescriptorHandle& handle ) { return m_Heap->Free( handle, HeapType ); }

    CD3D12DescriptorPoolHeap* m_Heap;
};