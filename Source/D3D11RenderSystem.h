#pragma once

ID3D11Device*           GetDevice();

ID3D11DeviceContext*    GetDeviceContext();

IDXGISwapChain*         GetSwapChain();

bool                    InitRenderSystem( HWND hWnd );

void                    FiniRenderSystem();
