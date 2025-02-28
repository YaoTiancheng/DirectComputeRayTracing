#pragma once

#include "D3D12DescriptorHandle.h"

class CD3D12GPUDescriptorHeap
{
public:
    CD3D12GPUDescriptorHeap()
        : m_Size( 0 ), m_Top( 0 )
    {
    }

    bool Create( D3D12_DESCRIPTOR_HEAP_TYPE heapType, uint32_t size );

    void Destroy();

    void Reset() { m_Top = 0; }

    ID3D12DescriptorHeap* GetD3DHeap() const { return m_Heap.Get(); }

    CD3D12DescritorHandle Allocate( D3D12_DESCRIPTOR_HEAP_TYPE type );

    CD3D12DescritorHandle AllocateRange( D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t number );

private:
    ComPtr<ID3D12DescriptorHeap> m_Heap;
    uint32_t m_Top;
    uint32_t m_Size;
};