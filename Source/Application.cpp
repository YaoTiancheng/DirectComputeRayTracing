
#include "stdafx.h"
#include "Application.h"
#include "D3D11RenderSystem.h"
#include "Scene.h"
#include "CommandLineArgs.h"

#define MAX_LOADSTRING 100

// Global Variables:
WCHAR szTitle[ MAX_LOADSTRING ];                  // The title bar text
WCHAR szWindowClass[ MAX_LOADSTRING ];            // the main window class name

Scene* g_Scene;

LRESULT CALLBACK WndProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam )
{
    switch ( message )
    {
    case WM_DESTROY:
    {
        PostQuitMessage( 0 );
        break;
    }
    default:
    {
        if ( !g_Scene || !g_Scene->OnWndMessage( message, wParam, lParam ) )
            return DefWindowProc( hWnd, message, wParam, lParam );
    }
    }
    return 0;
}

ATOM MyRegisterClass( HINSTANCE hInstance )
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof( WNDCLASSEX );

    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon( hInstance, MAKEINTRESOURCE( IDI_DIRECTCOMPUTERAYTRACING ) );
    wcex.hCursor = LoadCursor( nullptr, IDC_ARROW );
    wcex.hbrBackground = ( HBRUSH ) ( COLOR_WINDOW + 1 );
    wcex.lpszMenuName = NULL;
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm = LoadIcon( wcex.hInstance, MAKEINTRESOURCE( IDI_SMALL ) );

    return RegisterClassExW( &wcex );
}

HWND InitInstance( HINSTANCE hInstance, int nCmdShow, ULONG width, ULONG height )
{
    DWORD style = WS_OVERLAPPEDWINDOW & ( ~WS_SIZEBOX );
    RECT rect;
    rect.left = 0;
    rect.right = width;
    rect.top = 0;
    rect.bottom = height;
    AdjustWindowRect( &rect, style, FALSE );

    HWND hWnd = CreateWindowW( szWindowClass, szTitle, style,
        CW_USEDEFAULT, 0, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, hInstance, nullptr );

    if ( !hWnd )
    {
        return NULL;
    }

    ShowWindow( hWnd, nCmdShow );
    UpdateWindow( hWnd );

    return hWnd;
}

int APIENTRY wWinMain( _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow )
{
    UNREFERENCED_PARAMETER( hPrevInstance );

    CommandLineArgs cmdlnArgs;
    cmdlnArgs.Parse( lpCmdLine );

    LoadStringW( hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING );
    LoadStringW( hInstance, IDC_DIRECTCOMPUTERAYTRACING, szWindowClass, MAX_LOADSTRING );
    MyRegisterClass( hInstance );

    HWND hWnd = InitInstance( hInstance, nCmdShow, cmdlnArgs.ResolutionX(), cmdlnArgs.ResolutionY() );
    if ( !hWnd )
        return FALSE;

    if ( !InitRenderSystem( hWnd ) )
        return FALSE;

    int retCode = 0;

    g_Scene = new Scene();
    if ( g_Scene->Init() )
    {
        if ( g_Scene->ResetScene() )
        {
            HACCEL hAccelTable = LoadAccelerators( hInstance, MAKEINTRESOURCE( IDC_DIRECTCOMPUTERAYTRACING ) );
            MSG msg;

            bool isRendering = true;
            while ( isRendering )
            {
                while ( PeekMessage( &msg, NULL, 0, 0, PM_REMOVE ) )
                {
                    if ( msg.message == WM_QUIT )
                    {
                        isRendering = false;
                        break;
                    }

                    if ( !TranslateAccelerator( msg.hwnd, hAccelTable, &msg ) )
                    {
                        TranslateMessage( &msg );
                        DispatchMessage( &msg );
                    }
                }

                g_Scene->AddOneSampleAndRender();

                GetSwapChain()->Present( 0, 0 );
            }

            retCode = (int)msg.wParam;
        }
    }

    delete g_Scene;

    FiniRenderSystem();

    return retCode;
}



