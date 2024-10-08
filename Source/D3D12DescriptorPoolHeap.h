#pragma once

#include "D3D12DescriptorHandle.h"

class CD3D12DescriptorPoolHeap
{
public:
    bool Create( D3D12_DESCRIPTOR_HEAP_TYPE heapType, uint32_t size );

    void Destroy();

    CD3D12DescritorHandle Allocate( ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* desc );

    CD3D12DescritorHandle Allocate( ID3D12Resource* resource, const D3D12_UNORDERED_ACCESS_VIEW_DESC* desc );

    CD3D12DescritorHandle Allocate( const D3D12_CONSTANT_BUFFER_VIEW_DESC* desc );

    CD3D12DescritorHandle Allocate( ID3D12Resource* resource, const D3D12_RENDER_TARGET_VIEW_DESC* desc );

    CD3D12DescritorHandle Allocate( const D3D12_SAMPLER_DESC* desc );

    void FreeCBVSRVUAV( const CD3D12DescritorHandle& handle );

    void FreeRTV( const CD3D12DescritorHandle& handle );

    void FreeSampler( const CD3D12DescritorHandle& handle );

private:
    ComPtr<ID3D12DescriptorHeap> m_Heap;
    std::list<uint32_t> m_FreeEntries;
};