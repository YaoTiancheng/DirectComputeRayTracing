#pragma once

ID3D11Device*           GetDevice();

ID3D11DeviceContext*    GetDeviceContext();

IDXGISwapChain*         GetSwapChain();

ID3DUserDefinedAnnotation* GetAnnotation();

bool                    InitRenderSystem( HWND hWnd );

void                    FiniRenderSystem();

void                    ResizeSwapChainBuffers( uint32_t width, uint32_t height );

void                    Present( UINT syncInterval );
