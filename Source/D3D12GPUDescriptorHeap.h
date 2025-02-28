#pragma once

#include "D3D12DescriptorHandle.h"

struct SD3D12GPUDescriptorHeapHandle
{
    static SD3D12GPUDescriptorHeapHandle GetNull()
    {
        SD3D12GPUDescriptorHeapHandle handle;
        handle.m_CPU = CD3DX12_CPU_DESCRIPTOR_HANDLE( CD3DX12_DEFAULT() );
        handle.m_GPU = CD3DX12_GPU_DESCRIPTOR_HANDLE( CD3DX12_DEFAULT() );
        return handle;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE m_CPU;
    D3D12_GPU_DESCRIPTOR_HANDLE m_GPU;
};

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

    SD3D12GPUDescriptorHeapHandle Allocate( D3D12_DESCRIPTOR_HEAP_TYPE type );

    SD3D12GPUDescriptorHeapHandle AllocateRange( D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t number );

private:
    ComPtr<ID3D12DescriptorHeap> m_Heap;
    uint32_t m_Top;
    uint32_t m_Size;
};