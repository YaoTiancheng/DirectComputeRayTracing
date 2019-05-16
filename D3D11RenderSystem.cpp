#include "stdafx.h"
#include "D3D11RenderSystem.h"

ID3D11Device*           g_Device = nullptr;
ID3D11DeviceContext*    g_DeviceContext = nullptr;
IDXGISwapChain*         g_SwapChain = nullptr;

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

bool InitRenderSystem( HWND hWnd )
{
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

    HRESULT hr = D3D11CreateDeviceAndSwapChain( NULL
        , D3D_DRIVER_TYPE_HARDWARE
        , NULL
        , D3D11_CREATE_DEVICE_SINGLETHREADED | D3D11_CREATE_DEVICE_DEBUG
        , NULL
        , 0
        , D3D11_SDK_VERSION
        , &swapChainDesc
        , &g_SwapChain
        , &g_Device
        , NULL
        , &g_DeviceContext );

    if ( FAILED( hr ) )
        return false;

    return true;
}

void FiniRenderSystem()
{
    g_SwapChain->Release();
    g_DeviceContext->Release();

    ID3D11Debug *d3dDebug;
    HRESULT hr = g_Device->QueryInterface( __uuidof( ID3D11Debug ), reinterpret_cast< void** >( &d3dDebug ) );
    if ( SUCCEEDED( hr ) )
        hr = d3dDebug->ReportLiveDeviceObjects( D3D11_RLDO_DETAIL );
    if ( d3dDebug )
        d3dDebug->Release();
    g_Device->Release();

    g_SwapChain = nullptr;
    g_DeviceContext = nullptr;
    g_Device = nullptr;
}

ID3DBlob* CompileFromFile( LPCWSTR filename, LPCSTR entryPoint, LPCSTR target )
{
    ID3DBlob* shaderBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;
    HRESULT hr = D3DCompileFromFile( filename, NULL, D3D_COMPILE_STANDARD_FILE_INCLUDE, entryPoint, target, D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION | D3DCOMPILE_PREFER_FLOW_CONTROL, 0, &shaderBlob, &errorBlob );
    if ( FAILED( hr ) )
    {
        if ( errorBlob )
        {
            OutputDebugStringA( ( char* ) errorBlob->GetBufferPointer() );
            errorBlob->Release();
        }

        if ( shaderBlob )
            shaderBlob->Release();
    }

    return shaderBlob;
}

ID3D11ComputeShader* CreateComputeShader( ID3DBlob* shaderBlob )
{
    ID3D11ComputeShader* computeShader = nullptr;
    HRESULT hr = GetDevice()->CreateComputeShader( shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), NULL, &computeShader );
    return computeShader;
}

ID3D11VertexShader* CreateVertexShader( ID3DBlob* shaderBlob )
{
    ID3D11VertexShader* vertexShader = nullptr;
    HRESULT hr = GetDevice()->CreateVertexShader( shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), NULL, &vertexShader );
    return vertexShader;
}

ID3D11PixelShader* CreatePixelShader( ID3DBlob* shaderBlob )
{
    ID3D11PixelShader* pixelShader = nullptr;
    HRESULT hr = GetDevice()->CreatePixelShader( shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), NULL, &pixelShader );
    return pixelShader;
}

