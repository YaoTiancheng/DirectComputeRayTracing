#pragma once

#include "D3D12DescriptorHandle.h"

template <D3D12_DESCRIPTOR_HEAP_TYPE HeapType>
class TD3D12DescriptorPoolHeapRef;
class CD3D12GPUDescriptorHeap;
class CD3D12MultiHeapArena;
class CD3D12MultiBufferArena;

namespace D3D12Adapter
{
    ID3D12Device* GetDevice();

    IDXGISwapChain3* GetSwapChain();

    ID3D12GraphicsCommandList* GetCommandList();

    ID3D12DebugCommandList1* GetDebugCommandList();

    ID3D12CommandQueue* GetCommandQueue();

    uint32_t GetBackbufferCount();

    uint32_t GetBackbufferIndex();

    uint32_t GetDescriptorSize( D3D12_DESCRIPTOR_HEAP_TYPE type );

    bool Init( HWND hWnd );

    void Destroy();

    bool WaitForGPU( bool currentFrame = true );

    void BeginCurrentFrame();

    bool MoveToNextFrame();

    void ResizeSwapChainBuffers( uint32_t width, uint32_t height );

    void Present( UINT syncInterval );

    template <D3D12_DESCRIPTOR_HEAP_TYPE HeapType>
    TD3D12DescriptorPoolHeapRef<HeapType> GetDescriptorPoolHeap();

    CD3D12GPUDescriptorHeap* GetGPUDescriptorHeap();

    CD3D12MultiHeapArena* GetUploadHeapArena();

    CD3D12MultiBufferArena* GetUploadBufferArena();

    ID3D12CommandSignature* GetDispatchIndirectCommandSignature();

    SD3D12DescriptorHandle GetNullBufferSRV();

    SD3D12DescriptorHandle GetNullBufferUAV();
}
