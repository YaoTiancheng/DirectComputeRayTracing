#include "stdafx.h"
#include "D3D11RenderSystem.h"
#include "CommandLineArgs.h"

#define SAFE_RELEASE( x ) if( x != nullptr ) { x->Release(); x = nullptr; }

ID3D11Device*           g_Device = nullptr;
ID3D11DeviceContext*    g_DeviceContext = nullptr;
IDXGISwapChain*         g_SwapChain = nullptr;
ID3DUserDefinedAnnotation* g_Annotation = nullptr;
bool g_SupportTearing = false;

ID3D11Device* GetDevice()
{
    return g_Device;
}

ID3D11DeviceContext* GetDeviceContext()
{
    return g_DeviceContext;
}

IDXGISwapChain* GetSwapChain()
{
    return g_SwapChain;
}

ID3DUserDefinedAnnotation* GetAnnotation()
{
    return g_Annotation;
}

bool InitRenderSystem( HWND hWnd )
{
    // Check tearing support
    {
        Microsoft::WRL::ComPtr<IDXGIFactory5> factory;
        HRESULT hr = CreateDXGIFactory1( IID_PPV_ARGS( &factory ) );
        BOOL allowTearing = FALSE;
        if ( SUCCEEDED( hr ) )
        {
            hr = factory->CheckFeatureSupport( DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof( allowTearing ) );
        }

        g_SupportTearing = SUCCEEDED( hr ) && allowTearing;
    }

    DXGI_SWAP_CHAIN_DESC swapChainDesc;
    ZeroMemory( &swapChainDesc, sizeof( DXGI_SWAP_CHAIN_DESC ) );
    swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
    swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
    swapChainDesc.BufferDesc.Format = DXGI_FORMAT::DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = 2;
    swapChainDesc.OutputWindow = hWnd;
    swapChainDesc.Windowed = TRUE;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT::DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.Flags = g_SupportTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    UINT flags = D3D11_CREATE_DEVICE_SINGLETHREADED;
    if ( CommandLineArgs::Singleton()->UseDebugDevice() )
        flags |= D3D11_CREATE_DEVICE_DEBUG;
    const D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_1 };
    HRESULT hr = D3D11CreateDeviceAndSwapChain( NULL
        , D3D_DRIVER_TYPE_HARDWARE
        , NULL
        , flags
        , featureLevels
        , (uint32_t)std::size( featureLevels )
        , D3D11_SDK_VERSION
        , &swapChainDesc
        , &g_SwapChain
        , &g_Device
        , NULL
        , &g_DeviceContext );

    if ( FAILED( hr ) )
        return false;

    hr = g_DeviceContext->QueryInterface( __uuidof( ID3DUserDefinedAnnotation ), (void**)&g_Annotation );
    if ( FAILED( hr ) )
        return false;

    return true;
}

void FiniRenderSystem()
{
    SAFE_RELEASE( g_Annotation );
    SAFE_RELEASE( g_SwapChain );
    SAFE_RELEASE( g_DeviceContext );

    if ( g_Device )
    {
        ID3D11Debug* d3dDebug;
        HRESULT hr = g_Device->QueryInterface( __uuidof( ID3D11Debug ), reinterpret_cast<void**>( &d3dDebug ) );
        if ( SUCCEEDED( hr ) )
            hr = d3dDebug->ReportLiveDeviceObjects( D3D11_RLDO_DETAIL );
        if ( d3dDebug )
            d3dDebug->Release();
        g_Device->Release();
        g_Device = nullptr;
    }
}

void ResizeSwapChainBuffers( uint32_t width, uint32_t height )
{
    DXGI_SWAP_CHAIN_DESC swapChainDesc;
    g_SwapChain->GetDesc( &swapChainDesc );
    g_SwapChain->ResizeBuffers( swapChainDesc.BufferCount, width, height, swapChainDesc.BufferDesc.Format, swapChainDesc.Flags );
}

void Present( UINT syncInterval )
{
    g_SwapChain->Present( syncInterval, g_SupportTearing ? DXGI_PRESENT_ALLOW_TEARING : 0 );
}

