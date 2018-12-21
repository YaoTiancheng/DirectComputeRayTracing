#pragma once

ID3D11Device*           GetDevice();

ID3D11DeviceContext*    GetDeviceContext();

IDXGISwapChain*         GetSwapChain();

bool                    InitRenderSystem(HWND hWnd);

void                    FiniRenderSystem();

ID3DBlob*               CompileFromFile(LPCWSTR filename, LPCSTR entryPoint, LPCSTR target);

ID3D11ComputeShader*    CreateComputeShader(ID3DBlob* shaderBlob);

ID3D11VertexShader*     CreateVertexShader(ID3DBlob* shaderBlob);

ID3D11PixelShader*      CreatePixelShader(ID3DBlob* shaderBlob);