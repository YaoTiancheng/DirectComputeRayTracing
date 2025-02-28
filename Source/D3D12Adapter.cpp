#include "stdafx.h"
#include "D3D12Adapter.h"
#include "D3D12DescriptorPoolHeap.h"
#include "D3D12GPUDescriptorHeap.h"
#include "CommandLineArgs.h"

using namespace Microsoft::WRL;

#define BACKBUFFER_COUNT 2
#define GPU_DESCRIPTOR_HEAP_SIZE 1024
#define DESCRIPTOR_POOL_HEAP_SIZE_CBV_SRV_UAV 512
#define DESCRIPTOR_POOL_HEAP_SIZE_RTV 8

ComPtr<ID3D12Device> g_Device;
ComPtr<ID3D12CommandQueue> g_CommandQueue;
ComPtr<IDXGISwapChain3> g_SwapChain;
ComPtr<ID3D12GraphicsCommandList> g_CommandList;
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

uint32_t D3D12Adapter::GetBackbufferCount()
{
    return BACKBUFFER_COUNT;
}

uint32_t GetBackbufferIndex()
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

SD3D12DescriptorHandle D3D12Adapter::GetNullBufferSRV()
{
    return g_NullBufferSRV;
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
    HRESULT hr = g_Device->CreateCommandQueue( &queueDesc, IID_PPV_ARGS( &g_CommandQueue ) );
    if ( FAILED( hr ) )
    {
        return false;
    }

    UINT dxgiFactoryFlags = CommandLineArgs::Singleton()->UseDebugDevice() ? DXGI_CREATE_FACTORY_DEBUG : 0;
    ComPtr<IDXGIFactory4> factory;
    HRESULT hr = CreateDXGIFactory2( dxgiFactoryFlags, IID_PPV_ARGS( &factory ) );
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
    HRESULT hr = factory->CreateSwapChainForHwnd( g_Device.Get(), hWnd, &swapChainDesc, nullptr, nullptr, swapChain.GetAddressOf() );
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

    hr = g_Device->CreateFence( g_FenceValues[ g_BackbufferIndex ], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( &g_Fence ) );
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

        D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
        desc.Format = DXGI_FORMAT_R8_UINT;
        desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.Buffer.FirstElement = 0;
        desc.Buffer.NumElements = 0;
        desc.Buffer.StructureByteStride = 0;
        g_Device->CreateShaderResourceView( nullptr, &desc, g_NullBufferSRV.CPU );
    }

    if ( !g_GPUDescriptorHeap.Create( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, GPU_DESCRIPTOR_HEAP_SIZE ) )
    {
        return false;
    }

    return true;
}

void D3D12Adapter::Destroy()
{
    g_GPUDescriptorHeap.Destroy();

    for ( CD3D12DescriptorPoolHeap& heap : g_DescriptorPoolHeaps )
    {
        heap.Destroy();
    }

    CloseHandle( g_FenceEvent );
    g_Fence.Reset();
    g_CommandList.Reset();

    for ( uint32_t index = 0; index < BACKBUFFER_COUNT; ++index )
    {
        g_CommandAllocators[ index ].Reset();
    }

    g_SwapChain.Reset();
    g_CommandQueue.Reset();

    if ( g_Device )
    {
        ComPtr<IDXGIDebug1> dxgiDebug;
        if ( SUCCEEDED( DXGIGetDebugInterface1( 0, IID_PPV_ARGS( &dxgiDebug ) ) ) )
        {
            dxgiDebug->ReportLiveObjects( DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_FLAGS( DXGI_DEBUG_RLO_DETAIL | DXGI_DEBUG_RLO_IGNORE_INTERNAL ) );
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
    g_CommandList->SetDescriptorHeaps( 1, &g_GPUDescriptorHeap.GetD3DHeap() );
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
