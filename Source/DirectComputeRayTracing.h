#pragma once

class CDirectComputeRayTracing
{
public:
    explicit CDirectComputeRayTracing( HWND hWnd );

    ~CDirectComputeRayTracing();

    bool Init();

    void RenderOneFrame();

    bool OnWndMessage( UINT message, WPARAM wParam, LPARAM lParam );
};