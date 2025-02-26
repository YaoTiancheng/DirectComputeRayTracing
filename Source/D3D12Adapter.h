#pragma once

class CD3D12DescriptorPoolHeap;
class CD3D12GPUDescriptorHeap;
class CD3D12BufferedGrowingHeapArena;
class CD3D12BufferedGrowingBufferArena;

namespace D3D12Adapter
{
    ID3D12Device* GetDevice();

    IDXGISwapChain3* GetSwapChain();

    ID3D12GraphicsCommandList* GetCommandList();

    ID3D12CommandQueue* GetCommandQueue();

    uint32_t GetBackbufferCount();

    uint32_t GetBackbufferIndex();

    uint32_t GetDescriptorSize( D3D12_DESCRIPTOR_HEAP_TYPE type );

    bool Init( HWND hWnd );

    void Destroy();

    bool WaitForGPU();

    void BeginCurrentFrame();

    bool MoveToNextFrame();

    void ResizeSwapChainBuffers( uint32_t width, uint32_t height );

    void Present( UINT syncInterval );

    CD3D12DescriptorPoolHeap* GetDescriptorPoolHeap( D3D12_DESCRIPTOR_HEAP_TYPE heapType );

    CD3D12GPUDescriptorHeap* GetGPUDescriptorHeap( D3D12_DESCRIPTOR_HEAP_TYPE heapType );

    CD3D12BufferedGrowingHeapArena* GetUploadHeapArena();

    CD3D12BufferedGrowingBufferArena* GetUploadBufferArena();

    ID3D12CommandSignature* GetDispatchIndirectCommandSignature();
}
