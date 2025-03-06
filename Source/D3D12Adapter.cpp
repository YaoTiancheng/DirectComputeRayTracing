#include "stdafx.h"
#include "D3D12Adapter.h"
#include "D3D12DescriptorPoolHeap.h"
#include "D3D12GPUDescriptorHeap.h"
#include "D3D12MemoryArena.h"
#include "CommandLineArgs.h"

using namespace Microsoft::WRL;

#define BACKBUFFER_COUNT 2
#define GPU_DESCRIPTOR_HEAP_SIZE 1024
#define GPU_DESCRIPTOR_HEAP_RESERVED 1 // Reserved for ImGui
#define DESCRIPTOR_POOL_HEAP_SIZE_CBV_SRV_UAV 512
#define DESCRIPTOR_POOL_HEAP_SIZE_RTV 8
#define UPLOAD_BUFFER_ARENA_BYTESIZE 1 * 1024 * 1024
#define UPLOAD_HEAP_ARENA_BYTESIZE 1 * 1024 * 1024

ComPtr<ID3D12Device> g_Device;
ComPtr<ID3D12CommandQueue> g_CommandQueue;
ComPtr<IDXGISwapChain3> g_SwapChain;
ComPtr<ID3D12GraphicsCommandList> g_CommandList;
ComPtr<ID3D12DebugCommandList1> g_DebugCommandList;
ComPtr<ID3D12CommandAllocator> g_CommandAllocators[ BACKBUFFER_COUNT ];
ComPtr<ID3D12Fence> g_Fence;
HANDLE g_FenceEvent = NULL;
uint64_t g_FenceValues[ BACKBUFFER_COUNT ] = {};
bool g_SupportTearing = false;
uint32_t g_BackbufferIndex = 0;

uint32_t g_DescriptorSizes[ D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES ];
CD3D12DescriptorPoolHeap g_DescriptorPoolHeaps[ D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES ];
CD3D12GPUDescriptorHeap g_GPUDescriptorHeap;
SD3D12DescriptorHandle g_NullBufferSRV;
SD3D12DescriptorHandle g_NullBufferUAV;

struct SUploadMemoryArena
{
    CD3D12MultiBufferArena m_UploadBufferArena;
    CD3D12MultiHeapArena m_UploadHeapArena;
};

SUploadMemoryArena g_UploadMemoryArenas[ BACKBUFFER_COUNT ];

ComPtr<ID3D12CommandSignature> g_DispatchIndirectCommandSignature;

ID3D12Device* D3D12Adapter::GetDevice()
{
    return g_Device.Get();
}

IDXGISwapChain3* D3D12Adapter::GetSwapChain()
{
    return g_SwapChain.Get();
}

ID3D12GraphicsCommandList* D3D12Adapter::GetCommandList()
{
    return g_CommandList.Get();
}

ID3D12DebugCommandList1* D3D12Adapter::GetDebugCommandList()
{
    return g_DebugCommandList.Get();
}

ID3D12CommandQueue* D3D12Adapter::GetCommandQueue()
{
    return g_CommandQueue.Get();
}

uint32_t D3D12Adapter::GetBackbufferCount()
{
    return BACKBUFFER_COUNT;
}

uint32_t D3D12Adapter::GetBackbufferIndex()
{
    return g_BackbufferIndex;
}

uint32_t D3D12Adapter::GetDescriptorSize( D3D12_DESCRIPTOR_HEAP_TYPE type )
{
    return g_DescriptorSizes[ (uint32_t)type ];
}

CD3D12DescriptorPoolHeap* D3D12Adapter::GetDescriptorPoolHeap( D3D12_DESCRIPTOR_HEAP_TYPE heapType )
{
    return &g_DescriptorPoolHeaps[ (uint32_t)heapType ];
}

CD3D12GPUDescriptorHeap* D3D12Adapter::GetGPUDescriptorHeap()
{
    return &g_GPUDescriptorHeap;
}

CD3D12MultiHeapArena* D3D12Adapter::GetUploadHeapArena()
{
    return &g_UploadMemoryArenas[ g_BackbufferIndex ].m_UploadHeapArena;
}

CD3D12MultiBufferArena* D3D12Adapter::GetUploadBufferArena()
{
    return &g_UploadMemoryArenas[ g_BackbufferIndex ].m_UploadBufferArena;
}

ID3D12CommandSignature* D3D12Adapter::GetDispatchIndirectCommandSignature()
{
    return g_DispatchIndirectCommandSignature.Get();
}

SD3D12DescriptorHandle D3D12Adapter::GetNullBufferSRV()
{
    return g_NullBufferSRV;
}

SD3D12DescriptorHandle D3D12Adapter::GetNullBufferUAV()
{
    return g_NullBufferUAV;
}

bool D3D12Adapter::Init( HWND hWnd )
{
    if ( CommandLineArgs::Singleton()->UseDebugDevice() )
    {
        ComPtr<ID3D12Debug> debugController;
        if ( SUCCEEDED( D3D12GetDebugInterface( IID_PPV_ARGS( &debugController ) ) ) )
        {
            debugController->EnableDebugLayer();
        }
    }

    // Check tearing support
    {
        ComPtr<IDXGIFactory5> factory;
        HRESULT hr = CreateDXGIFactory1( IID_PPV_ARGS( &factory ) );
        BOOL allowTearing = FALSE;
        if ( SUCCEEDED( hr ) )
        {
            hr = factory->CheckFeatureSupport( DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof( allowTearing ) );
        }

        g_SupportTearing = SUCCEEDED( hr ) && allowTearing;
    }

    HRESULT hr = D3D12CreateDevice( nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS( &g_Device ) );
    if ( FAILED( hr ) )
    {
        return false;
    }

    // Describe and create the command queue.
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = g_Device->CreateCommandQueue( &queueDesc, IID_PPV_ARGS( &g_CommandQueue ) );
    if ( FAILED( hr ) )
    {
        return false;
    }

    UINT dxgiFactoryFlags = CommandLineArgs::Singleton()->UseDebugDevice() ? DXGI_CREATE_FACTORY_DEBUG : 0;
    ComPtr<IDXGIFactory4> factory;
    hr = CreateDXGIFactory2( dxgiFactoryFlags, IID_PPV_ARGS( &factory ) );
    if ( FAILED( hr ) )
    {
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Format = DXGI_FORMAT::DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = BACKBUFFER_COUNT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT::DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.Flags = g_SupportTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    ComPtr<IDXGISwapChain1> swapChain;
    hr = factory->CreateSwapChainForHwnd( g_CommandQueue.Get(), hWnd, &swapChainDesc, nullptr, nullptr, swapChain.GetAddressOf() );
    if ( FAILED( hr ) )
    {
        return false;
    }

    swapChain.As( &g_SwapChain );

    for ( uint32_t index = 0; index < BACKBUFFER_COUNT; ++index )
    {
        hr = g_Device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( &g_CommandAllocators[ index ] ) );
        if ( FAILED( hr ) )
        {
            return false;
        }
    }

    g_BackbufferIndex = g_SwapChain->GetCurrentBackBufferIndex();

    hr = g_Device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_CommandAllocators[ g_BackbufferIndex ].Get(), nullptr, IID_PPV_ARGS( &g_CommandList ) );
    if ( FAILED( hr ) )
    {
        return false;
    }

    if ( CommandLineArgs::Singleton()->UseDebugDevice() )
    { 
        g_CommandList.As( &g_DebugCommandList );
    }

    hr = g_Device->CreateFence( g_FenceValues[ g_BackbufferIndex ], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( &g_Fence ) );
    g_FenceValues[ g_BackbufferIndex ]++;
    if ( FAILED( hr ) )
    {
        return false;
    }

    g_FenceEvent = CreateEvent( nullptr, FALSE, FALSE, nullptr );
    if ( !g_FenceEvent )
    {
        return false;
    }

    for ( uint32_t type = 0; type < (uint32_t)D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++type )
    {
        g_DescriptorSizes[ type ] = g_Device->GetDescriptorHandleIncrementSize( (D3D12_DESCRIPTOR_HEAP_TYPE)type );
    }

    // Only create two heaps, leave others unused.
    if ( !g_DescriptorPoolHeaps[ (uint32_t)D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ].Create( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, DESCRIPTOR_POOL_HEAP_SIZE_CBV_SRV_UAV ) )
    {
        return false;
    }
    if ( !g_DescriptorPoolHeaps[ (uint32_t)D3D12_DESCRIPTOR_HEAP_TYPE_RTV ].Create( D3D12_DESCRIPTOR_HEAP_TYPE_RTV, DESCRIPTOR_POOL_HEAP_SIZE_RTV ) )
    {
        return false;
    }

    // Create null SRV
    {
        g_NullBufferSRV = g_DescriptorPoolHeaps[ (uint32_t)D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ].Allocate( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
        g_NullBufferUAV = g_DescriptorPoolHeaps[ (uint32_t)D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ].Allocate( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

        {
            D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
            desc.Format = DXGI_FORMAT_R8_UINT;
            desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            g_Device->CreateShaderResourceView( nullptr, &desc, g_NullBufferSRV.CPU );
        }
        
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
            desc.Format = DXGI_FORMAT_R8_UINT;
            desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            g_Device->CreateUnorderedAccessView( nullptr, nullptr, &desc, g_NullBufferUAV.CPU );
        }
    }

    if ( !g_GPUDescriptorHeap.Create( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, GPU_DESCRIPTOR_HEAP_RESERVED, GPU_DESCRIPTOR_HEAP_SIZE ) )
    {
        return false;
    }

    for ( SUploadMemoryArena& arena : g_UploadMemoryArenas )
    {
        // Create buffer arena
        {
            CD3D12BufferArena::SInitializer initializer = {};
            initializer.m_HeapProperties = CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_UPLOAD );
            initializer.m_State = D3D12_RESOURCE_STATE_GENERIC_READ;
            initializer.SizeInBytes = UPLOAD_BUFFER_ARENA_BYTESIZE;
            if ( !arena.m_UploadBufferArena.Create( initializer, 1 ) )
            {
                return false;
            }
        }

        // Create heap arena
        {
            D3D12_HEAP_DESC initializer = {};
            initializer.SizeInBytes = UPLOAD_HEAP_ARENA_BYTESIZE;
            initializer.Properties = CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_UPLOAD );
            initializer.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
            if ( !arena.m_UploadHeapArena.Create( initializer, 1 ) )
            {
                return false;
            }
        }
    }

    {
        D3D12_INDIRECT_ARGUMENT_DESC arg = {};
        arg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
        D3D12_COMMAND_SIGNATURE_DESC desc = {};
        desc.ByteStride = 12;
        desc.NumArgumentDescs = 1;
        desc.pArgumentDescs = &arg;
        hr = g_Device->CreateCommandSignature( &desc, nullptr, IID_PPV_ARGS( g_DispatchIndirectCommandSignature.GetAddressOf() ) );
        if ( FAILED( hr ) )
        {
            return false;
        }
    }

    // Bind descriptor heaps
    ID3D12DescriptorHeap* heap = g_GPUDescriptorHeap.GetD3DHeap();
    g_CommandList->SetDescriptorHeaps( 1, &heap );

    return true;
}

void D3D12Adapter::Destroy()
{
    g_DispatchIndirectCommandSignature.Reset();

    for ( SUploadMemoryArena& arena : g_UploadMemoryArenas )
    {
        arena.m_UploadBufferArena.Destroy();
        arena.m_UploadHeapArena.Destroy();
    }

    g_GPUDescriptorHeap.Destroy();

    for ( CD3D12DescriptorPoolHeap& heap : g_DescriptorPoolHeaps )
    {
        heap.Destroy();
    }

    CloseHandle( g_FenceEvent );
    g_Fence.Reset();
    g_DebugCommandList.Reset();
    g_CommandList.Reset();

    for ( uint32_t index = 0; index < BACKBUFFER_COUNT; ++index )
    {
        g_CommandAllocators[ index ].Reset();
    }

    g_SwapChain.Reset();
    g_CommandQueue.Reset();

    if ( g_Device )
    {
        ComPtr<ID3D12DebugDevice> debugDevice;
        g_Device.As( &debugDevice );
        if ( debugDevice )
        { 
            debugDevice->ReportLiveDeviceObjects( D3D12_RLDO_DETAIL | D3D12_RLDO_IGNORE_INTERNAL );
        }
        g_Device.Reset();
    }
}

bool D3D12Adapter::WaitForGPU()
{
    // Schedule a Signal command in the queue.
    if ( FAILED( g_CommandQueue->Signal( g_Fence.Get(), g_FenceValues[ g_BackbufferIndex ] ) ) )
    {
        return false;
    }

    // Wait until the fence has been processed.
    if ( FAILED( g_Fence->SetEventOnCompletion( g_FenceValues[ g_BackbufferIndex ], g_FenceEvent ) ) )
    {
        return false;
    }

    WaitForSingleObjectEx( g_FenceEvent, INFINITE, FALSE );

    // Increment the fence value for the current frame.
    g_FenceValues[ g_BackbufferIndex ]++;

    return true;
}

void D3D12Adapter::BeginCurrentFrame()
{
    // Command list allocators can only be reset when the associated 
    // command lists have finished execution on the GPU; apps should use 
    // fences to determine GPU execution progress.
    g_CommandAllocators[ g_BackbufferIndex ]->Reset();

    // However, when ExecuteCommandList() is called on a particular command 
    // list, that command list can then be reset at any time and must be before 
    // re-recording.
    g_CommandList->Reset( g_CommandAllocators[ g_BackbufferIndex ].Get(), nullptr );

    // Bind descriptor heaps
    ID3D12DescriptorHeap* heap = g_GPUDescriptorHeap.GetD3DHeap();
    g_CommandList->SetDescriptorHeaps( 1, &heap );
}

bool D3D12Adapter::MoveToNextFrame()
{
    // Schedule a Signal command in the queue.
    const uint64_t currentFenceValue = g_FenceValues[ g_BackbufferIndex ];
    if ( FAILED( g_CommandQueue->Signal( g_Fence.Get(), currentFenceValue ) ) )
    {
        return false;
    }

    // Update the backbuffer index.
    g_BackbufferIndex = g_SwapChain->GetCurrentBackBufferIndex();

    // If the next frame is not ready to be rendered yet, wait until it is ready.
    if ( g_Fence->GetCompletedValue() < g_FenceValues[ g_BackbufferIndex ] )
    {
        if ( FAILED( g_Fence->SetEventOnCompletion( g_FenceValues[ g_BackbufferIndex ], g_FenceEvent ) ) )
        {
            return false;
        }
        WaitForSingleObjectEx( g_FenceEvent, INFINITE, FALSE );
    }

    // Set the fence value for the next frame.
    g_FenceValues[ g_BackbufferIndex ] = currentFenceValue + 1;

    // Reset the GPU descriptor heap
    g_GPUDescriptorHeap.Reset();

    // Reset the upload memory arenas
    SUploadMemoryArena& memoryArena = g_UploadMemoryArenas[ g_BackbufferIndex ];
    memoryArena.m_UploadBufferArena.Reset( 1 );
    memoryArena.m_UploadHeapArena.Reset( 1 );

    return true;
}

void D3D12Adapter::ResizeSwapChainBuffers( uint32_t width, uint32_t height )
{
    DXGI_SWAP_CHAIN_DESC swapChainDesc;
    g_SwapChain->GetDesc( &swapChainDesc );
    g_SwapChain->ResizeBuffers( swapChainDesc.BufferCount, width, height, swapChainDesc.BufferDesc.Format, swapChainDesc.Flags );
}

void D3D12Adapter::Present( UINT syncInterval )
{
    g_SwapChain->Present( syncInterval, g_SupportTearing ? DXGI_PRESENT_ALLOW_TEARING : 0 );
}
