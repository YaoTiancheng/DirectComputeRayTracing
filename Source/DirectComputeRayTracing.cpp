#include "stdafx.h"
#include "DirectComputeRayTracing.h"
#include "D3D11RenderSystem.h"
#include "CommandLineArgs.h"
#include "GPUTexture.h"
#include "GPUBuffer.h"
#include "Logging.h"
#include "StringConversion.h"
#include "Camera.h"
#include "PostProcessingRenderer.h"
#include "Timers.h"
#include "Rectangle.h"
#include "BxDFTexturesBuilder.h"
#include "RenderContext.h"
#include "RenderData.h"
#include "MegakernelPathTracer.h"
#include "WavefrontPathTracer.h"
#include "Scene.h"
#include "MessageBox.h"
#include "ScopedRenderAnnotation.h"
#include "SampleConvolutionRenderer.h"
#include "Constants.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_win32.h"
#include "../Shaders/SumLuminanceDef.inc.hlsl"

using namespace DirectX;

struct SRenderer
{
    explicit SRenderer( HWND hWnd )
        : m_hWnd( hWnd )
    {
    }

    ~SRenderer();

    bool OnWndMessage( UINT message, WPARAM wParam, LPARAM lParam );

    bool Init();

    bool LoadScene( const char* filepath, bool reset );

    void DispatchRayTracing( SRenderContext* renderContext );

    void RenderOneFrame();

    void UpdateGPUData();

    void ClearFilmTexture();

    void UpdateRenderViewport( uint32_t backbufferWidth, uint32_t backbufferHeight );

    void ResizeBackbuffer( uint32_t backbufferWidth, uint32_t backbufferHeight );

    void OnImGUI( SRenderContext* renderContext );

    HWND m_hWnd;

    uint32_t m_FrameSeed = 0;

    bool m_IsLightGPUBufferDirty;
    bool m_IsMaterialGPUBufferDirty;
    bool m_IsFilmDirty = true;
    bool m_IsLastFrameFilmDirty = true;

    SRenderData m_RenderData;
    CScene m_Scene;
    CPathTracer* m_PathTracer[ 2 ] = { nullptr, nullptr };
    uint32_t m_ActivePathTracerIndex = 0;
    CSampleConvolutionRenderer m_SampleConvolutionRenderer;
    PostProcessingRenderer m_PostProcessing;

    enum class EFrameSeedType { FrameIndex = 0, SampleCount = 1, Fixed = 2, _Count = 3 };
    EFrameSeedType m_FrameSeedType = EFrameSeedType::SampleCount;

    uint32_t m_ResolutionWidth;
    uint32_t m_ResolutionHeight;
    uint32_t m_SmallResolutionWidth = 480;
    uint32_t m_SmallResolutionHeight = 270;

    FrameTimer m_FrameTimer;

    SRectangle m_RenderViewport;

    bool m_RayTracingHasHit = false;
    SRayHit m_RayTracingHit;
    uint32_t m_RayTracingPixelPos[ 2 ] = { 0, 0 };
    float m_RayTracingSubPixelPos[ 2 ] = { 0.f, 0.f };

    uint32_t m_SPP;
    uint32_t m_CursorPixelPosOnRenderViewport[ 2 ];
    uint32_t m_CursorPixelPosOnFilm[ 2 ];
    bool m_ShowUI = true;
    bool m_ShowRayTracingUI = false;
};

SRenderer* s_Renderer = nullptr;

struct RayTracingFrameConstants
{
    uint32_t frameSeed;
    uint32_t padding[ 3 ];
};

static bool InitImGui( HWND hWnd )
{
    IMGUI_CHECKVERSION();

    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    if ( !ImGui_ImplWin32_Init( hWnd ) )
    {
        ImGui::DestroyContext();
        return false;
    }

    if ( !ImGui_ImplDX11_Init( GetDevice(), GetDeviceContext() ) )
    {
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    return true;
}

static void ShutDownImGui()
{
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

static void ImGUINewFrame()
{
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

CDirectComputeRayTracing::CDirectComputeRayTracing( HWND hWnd )
{
    s_Renderer = new SRenderer( hWnd );
}

CDirectComputeRayTracing::~CDirectComputeRayTracing()
{
    delete s_Renderer;
    ShutDownImGui();
    FiniRenderSystem();
}

bool CDirectComputeRayTracing::Init()
{
    if ( !InitRenderSystem( s_Renderer->m_hWnd ) )
        return false;

    if ( !InitImGui( s_Renderer->m_hWnd ) )
        return false;

    return s_Renderer->Init();
}

void CDirectComputeRayTracing::RenderOneFrame()
{
    s_Renderer->RenderOneFrame();
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam );

bool CDirectComputeRayTracing::OnWndMessage( UINT message, WPARAM wParam, LPARAM lParam )
{
    if ( ImGui_ImplWin32_WndProcHandler( s_Renderer->m_hWnd, message, wParam, lParam ) )
        return true;

    if ( message == WM_SIZE )
    {
        UINT width = LOWORD( lParam );
        UINT height = HIWORD( lParam );
        s_Renderer->ResizeBackbuffer( width, height );
        s_Renderer->UpdateRenderViewport( width, height );
    }

    return s_Renderer->OnWndMessage( message, wParam, lParam );
}

SRenderer::~SRenderer()
{
    m_PathTracer[ m_ActivePathTracerIndex ]->Destroy();
    for ( auto& it : m_PathTracer )
    {
        delete it;
    }
}

bool SRenderer::OnWndMessage( UINT message, WPARAM wParam, LPARAM lParam )
{
    return m_Scene.m_Camera.OnWndMessage( message, wParam, lParam );
}

bool SRenderer::Init()
{
    m_PathTracer[ 0 ] = new CMegakernelPathTracer( &m_Scene );
    m_PathTracer[ 1 ] = new CWavefrontPathTracer( &m_Scene );

    if ( !m_PathTracer[ m_ActivePathTracerIndex ]->Create() )
    {
        return false;
    }

    m_ResolutionWidth = CommandLineArgs::Singleton()->ResolutionX();
    m_ResolutionHeight = CommandLineArgs::Singleton()->ResolutionY();

    ID3D11Device* device = GetDevice();

    m_RenderData.m_FilmTexture.reset( GPUTexture::Create(
          m_ResolutionWidth
        , m_ResolutionHeight
        , DXGI_FORMAT_R32G32B32A32_FLOAT
        , GPUResourceCreationFlags_HasUAV | GPUResourceCreationFlags_IsRenderTarget ) );
    if ( !m_RenderData.m_FilmTexture )
        return false;

    m_RenderData.m_SamplePositionTexture.reset( GPUTexture::Create(
          m_ResolutionWidth
        , m_ResolutionHeight
        , DXGI_FORMAT_R32G32_FLOAT
        , GPUResourceCreationFlags_HasUAV ) );
    if ( !m_RenderData.m_SamplePositionTexture )
        return false;

    m_RenderData.m_SampleValueTexture.reset( GPUTexture::Create(
          m_ResolutionWidth
        , m_ResolutionHeight
        , DXGI_FORMAT_R32G32B32A32_FLOAT
        , GPUResourceCreationFlags_HasUAV ) );
    if ( !m_RenderData.m_SampleValueTexture )
        return false;

    m_RenderData.m_CookTorranceCompETexture.reset( BxDFTexturesBuilder::CreateCoorkTorranceBRDFEnergyTexture() );
    if ( !m_RenderData.m_CookTorranceCompETexture )
        return false;

    m_RenderData.m_CookTorranceCompEAvgTexture.reset( BxDFTexturesBuilder::CreateCookTorranceBRDFAverageEnergyTexture() );
    if ( !m_RenderData.m_CookTorranceCompEAvgTexture )
        return false;

    m_RenderData.m_CookTorranceCompInvCDFTexture.reset( BxDFTexturesBuilder::CreateCookTorranceMultiscatteringBRDFInvCDFTexture() );
    if ( !m_RenderData.m_CookTorranceCompInvCDFTexture )
        return false;

    m_RenderData.m_CookTorranceCompPdfScaleTexture.reset( BxDFTexturesBuilder::CreateCookTorranceMultiscatteringBRDFPDFScaleTexture() );
    if ( !m_RenderData.m_CookTorranceCompPdfScaleTexture )
        return false;

    m_RenderData.m_CookTorranceCompEFresnelTexture.reset( BxDFTexturesBuilder::CreateCoorkTorranceBRDFEnergyFresnelDielectricTexture() );
    if ( !m_RenderData.m_CookTorranceCompEFresnelTexture )
        return false;

    m_RenderData.m_CookTorranceBSDFETexture.reset( BxDFTexturesBuilder::CreateCookTorranceBSDFEnergyFresnelDielectricTexture() );
    if ( !m_RenderData.m_CookTorranceBSDFETexture )
        return false;

    m_RenderData.m_CookTorranceBSDFAvgETexture.reset( BxDFTexturesBuilder::CreateCookTorranceBSDFAverageEnergyTexture() );
    if ( !m_RenderData.m_CookTorranceBSDFAvgETexture )
        return false;

    m_RenderData.m_CookTorranceBTDFETexture.reset( BxDFTexturesBuilder::CreateCookTorranceBTDFEnergyTexture() );
    if ( !m_RenderData.m_CookTorranceBTDFETexture )
        return false;

    m_RenderData.m_CookTorranceBSDFInvCDFTexture.reset( BxDFTexturesBuilder::CreateCookTorranceBSDFMultiscatteringInvCDFTexture() );
    if ( !m_RenderData.m_CookTorranceBSDFInvCDFTexture )
        return false;

    m_RenderData.m_CookTorranceBSDFPDFScaleTexture.reset( BxDFTexturesBuilder::CreateCookTorranceBSDFMultiscatteringPDFScaleTexture() );
    if ( !m_RenderData.m_CookTorranceBSDFPDFScaleTexture )
        return false;

    m_RenderData.m_RayTracingFrameConstantBuffer.reset( GPUBuffer::Create(
          sizeof( RayTracingFrameConstants )
        , 0
        , DXGI_FORMAT_UNKNOWN
        , D3D11_USAGE_DYNAMIC
        , D3D11_BIND_CONSTANT_BUFFER
        , GPUResourceCreationFlags_CPUWriteable ) );
    if ( !m_RenderData.m_RayTracingFrameConstantBuffer )
        return false;

    m_RenderData.m_RenderResultTexture.reset( GPUTexture::Create( m_ResolutionWidth, m_ResolutionHeight, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, GPUResourceCreationFlags_IsRenderTarget ) );
    if ( !m_RenderData.m_RenderResultTexture )
        return false;

    m_RenderData.m_sRGBBackbuffer.reset( GPUTexture::CreateFromSwapChain( DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ) );
    if ( !m_RenderData.m_sRGBBackbuffer )
        return false;

    m_RenderData.m_LinearBackbuffer.reset( GPUTexture::CreateFromSwapChain() );
    if ( !m_RenderData.m_LinearBackbuffer )
        return false;

    D3D11_SAMPLER_DESC samplerDesc;
    ZeroMemory( &samplerDesc, sizeof( D3D11_SAMPLER_DESC ) );
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MaxAnisotropy = 1;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    HRESULT hr = device->CreateSamplerState( &samplerDesc, &m_RenderData.m_UVClampSamplerState );
    if ( FAILED( hr ) )
        return false;

    if ( !m_SampleConvolutionRenderer.Init() )
        return false;

    if ( !m_PostProcessing.Init( m_ResolutionWidth, m_ResolutionHeight, m_RenderData.m_FilmTexture, m_RenderData.m_RenderResultTexture ) )
        return false;

    UpdateRenderViewport( m_ResolutionWidth, m_ResolutionHeight );

    LoadScene( CommandLineArgs::Singleton()->GetFilename().c_str(), true );

    return true;
}

bool SRenderer::LoadScene( const char* filepath, bool reset )
{
    m_IsFilmDirty = true; // Clear film in case scene reset failed and ray tracing being disabled.

    if ( reset )
    {
        m_Scene.Reset();
    }

    bool loadSceneResult = m_Scene.LoadFromFile( filepath );
    if ( !loadSceneResult )
    {
        return false;
    }

    m_PathTracer[ m_ActivePathTracerIndex ]->OnSceneLoaded();

    m_IsMaterialGPUBufferDirty = true;
    m_IsLightGPUBufferDirty = true;

    m_RayTracingHasHit = false;

    return true;
}

void SRenderer::DispatchRayTracing( SRenderContext* renderContext )
{
    m_IsFilmDirty = m_IsFilmDirty || m_IsLightGPUBufferDirty || m_IsMaterialGPUBufferDirty || m_Scene.m_Camera.IsDirty() || m_PathTracer[ m_ActivePathTracerIndex ]->AcquireFilmClearTrigger();

    renderContext->m_IsResolutionChanged = ( m_IsFilmDirty != m_IsLastFrameFilmDirty );
    renderContext->m_IsSmallResolutionEnabled = m_IsFilmDirty;

    m_IsLastFrameFilmDirty = m_IsFilmDirty;

    renderContext->m_CurrentResolutionWidth = renderContext->m_IsSmallResolutionEnabled ? m_SmallResolutionWidth : m_ResolutionWidth;
    renderContext->m_CurrentResolutionRatio = (float)renderContext->m_CurrentResolutionWidth / m_ResolutionWidth;
    renderContext->m_CurrentResolutionHeight = renderContext->m_IsSmallResolutionEnabled ? m_SmallResolutionHeight : m_ResolutionHeight;

    if ( m_IsFilmDirty || renderContext->m_IsResolutionChanged )
    {
        ClearFilmTexture();

        if ( m_FrameSeedType == EFrameSeedType::SampleCount )
        {
            m_FrameSeed = 0;
        }

        m_SPP = 0;

        m_PathTracer[ m_ActivePathTracerIndex ]->ResetImage();
    }

    if ( m_Scene.m_HasValidScene )
    {
        if ( m_IsLightGPUBufferDirty )
        {
            m_Scene.UpdateLightGPUData();
        }

        if ( m_IsMaterialGPUBufferDirty )
        {
            m_Scene.UpdateMaterialGPUData();
        }

        UpdateGPUData();

        m_PathTracer[ m_ActivePathTracerIndex ]->Render( *renderContext, m_RenderData );

        if ( m_PathTracer[ m_ActivePathTracerIndex ]->IsImageComplete() )
        {
            if ( m_FrameSeedType != EFrameSeedType::Fixed )
            {
                m_FrameSeed++;
            }

            ++m_SPP;
        }
    }

    m_Scene.m_Camera.ClearDirty();
    m_IsLightGPUBufferDirty = false;
    m_IsMaterialGPUBufferDirty = false;
    m_IsFilmDirty = false;
}

void SRenderer::RenderOneFrame()
{
    SRenderContext renderContext;
    renderContext.m_EnablePostFX = true;

    m_FrameTimer.BeginFrame();

    m_Scene.m_Camera.Update( m_FrameTimer.GetCurrentFrameDeltaTime() );

    DispatchRayTracing( &renderContext );

    ID3D11RenderTargetView* RTV = nullptr;
    D3D11_VIEWPORT viewport;
    ID3D11DeviceContext* deviceContext = GetDeviceContext();

    if ( m_PathTracer[ m_ActivePathTracerIndex ]->IsImageComplete() || renderContext.m_IsSmallResolutionEnabled )
    {
        m_SampleConvolutionRenderer.Execute( renderContext, m_Scene, m_RenderData );

        m_PostProcessing.ExecuteLuminanceCompute( renderContext );

        RTV = m_RenderData.m_RenderResultTexture->GetRTV();
        deviceContext->OMSetRenderTargets( 1, &RTV, nullptr );

        viewport = { 0.0f, 0.0f, (float)m_ResolutionWidth, (float)m_ResolutionHeight, 0.0f, 1.0f };
        deviceContext->RSSetViewports( 1, &viewport );

        m_PostProcessing.ExecutePostFX( renderContext, m_Scene );
    }

    RTV = m_RenderData.m_sRGBBackbuffer->GetRTV();
    deviceContext->OMSetRenderTargets( 1, &RTV, nullptr );

    DXGI_SWAP_CHAIN_DESC swapChainDesc;
    GetSwapChain()->GetDesc( &swapChainDesc );
    viewport = { 0.0f, 0.0f, (float)swapChainDesc.BufferDesc.Width, (float)swapChainDesc.BufferDesc.Height, 0.0f, 1.0f };
    deviceContext->RSSetViewports( 1, &viewport );

    XMFLOAT4 clearColor = { 0.0f, 0.0f, 0.0f, 0.0f };
    deviceContext->ClearRenderTargetView( RTV, (float*)&clearColor );

    viewport = { (float)m_RenderViewport.m_TopLeftX, (float)m_RenderViewport.m_TopLeftY, (float)m_RenderViewport.m_Width, (float)m_RenderViewport.m_Height, 0.0f, 1.0f };
    deviceContext->RSSetViewports( 1, &viewport );

    m_PostProcessing.ExecuteCopy();

    ImGUINewFrame();
    OnImGUI( &renderContext );
    ImGui::Render();

    RTV = m_RenderData.m_LinearBackbuffer->GetRTV();
    GetDeviceContext()->OMSetRenderTargets( 1, &RTV, nullptr );

    {
        SCOPED_RENDER_ANNOTATION( L"ImGUI" );
        ImGui_ImplDX11_RenderDrawData( ImGui::GetDrawData() );
    }

    RTV = nullptr;
    GetDeviceContext()->OMSetRenderTargets( 1, &RTV, nullptr );

    Present( 0 );
}

void SRenderer::UpdateGPUData()
{
    if ( void* address = m_RenderData.m_RayTracingFrameConstantBuffer->Map() )
    {
        RayTracingFrameConstants* constants = (RayTracingFrameConstants*)address;
        constants->frameSeed = m_FrameSeed;
        m_RenderData.m_RayTracingFrameConstantBuffer->Unmap();
    }
}

void SRenderer::ClearFilmTexture()
{
    ID3D11DeviceContext* deviceContext = GetDeviceContext();
    const static float kClearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    deviceContext->ClearRenderTargetView( m_RenderData.m_FilmTexture->GetRTV(), kClearColor );
}

void SRenderer::UpdateRenderViewport( uint32_t backbufferWidth, uint32_t backbufferHeight )
{
    uint32_t renderWidth = m_ResolutionWidth;
    uint32_t renderHeight = m_ResolutionHeight;
    float scale = (float)backbufferWidth / renderWidth;
    float desiredViewportHeight = renderHeight * scale;
    if ( desiredViewportHeight > backbufferHeight )
    {
        scale = (float)backbufferHeight / renderHeight;
    }

    m_RenderViewport.m_Width = uint32_t( renderWidth * scale );
    m_RenderViewport.m_Height = uint32_t( renderHeight * scale );
    m_RenderViewport.m_TopLeftX = uint32_t( ( backbufferWidth - m_RenderViewport.m_Width ) * 0.5f );
    m_RenderViewport.m_TopLeftY = uint32_t( ( backbufferHeight - m_RenderViewport.m_Height ) * 0.5f );
}

void SRenderer::ResizeBackbuffer( uint32_t backbufferWidth, uint32_t backbufferHeight )
{
    m_RenderData.m_sRGBBackbuffer.reset();
    m_RenderData.m_LinearBackbuffer.reset();

    ResizeSwapChainBuffers( backbufferWidth, backbufferHeight );

    m_RenderData.m_sRGBBackbuffer.reset( GPUTexture::CreateFromSwapChain( DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ) );
    m_RenderData.m_LinearBackbuffer.reset( GPUTexture::CreateFromSwapChain() );
}

void SRenderer::OnImGUI( SRenderContext* renderContext )
{
    if ( ImGui::GetIO().KeysDown[ VK_F1 ] && ImGui::GetIO().KeysDownDuration[ VK_F1 ] == 0.0f )
    {
        m_ShowUI = !m_ShowUI;
    }
    if ( ImGui::GetIO().KeysDown[ VK_F2 ] && ImGui::GetIO().KeysDownDuration[ VK_F2 ] == 0.0f )
    {
        m_ShowRayTracingUI = !m_ShowRayTracingUI;
    }

    if ( !m_ShowUI )
        return;

    {
        ImGui::Begin( "Settings" );
        ImGui::PushItemWidth( ImGui::GetFontSize() * -15 );

        if ( ImGui::CollapsingHeader( "General" ) )
        {
            static const char* s_FrameSeedTypeNames[] = { "Frame Index", "Sample Count", "Fixed" };
            if ( ImGui::Combo( "Frame Seed Type", (int*)&m_FrameSeedType, s_FrameSeedTypeNames, IM_ARRAYSIZE( s_FrameSeedTypeNames ) ) )
            {
                m_IsFilmDirty = true;
            }

            if ( m_FrameSeedType == EFrameSeedType::Fixed )
            {
                if ( ImGui::InputInt( "Frame Seed", (int*)&m_FrameSeed, 1 ) )
                {
                    m_IsFilmDirty = true;
                }
            }

            ImGui::DragInt( "Small Resolution Width", (int*)&m_SmallResolutionWidth, 16, 16, m_ResolutionWidth, "%d", ImGuiSliderFlags_AlwaysClamp );
            ImGui::DragInt( "Small Resolution Height", (int*)&m_SmallResolutionHeight, 16, 16, m_ResolutionHeight, "%d", ImGuiSliderFlags_AlwaysClamp );

            uint32_t lastActivePathTracerIndex = m_ActivePathTracerIndex;
            static const char* s_PathTracerNames[] = { "Megakernel Path Tracer", "Wavefront Path Tracer" };
            if ( ImGui::Combo( "Path Tracer", (int*)&m_ActivePathTracerIndex, s_PathTracerNames, IM_ARRAYSIZE( s_PathTracerNames ) ) )
            {
                m_PathTracer[ lastActivePathTracerIndex ]->Destroy();
                m_PathTracer[ m_ActivePathTracerIndex ]->Create();
                m_PathTracer[ m_ActivePathTracerIndex ]->ResetImage();
                if ( m_Scene.m_HasValidScene )
                {
                    m_PathTracer[ m_ActivePathTracerIndex ]->OnSceneLoaded();
                }
                m_IsFilmDirty = true;
            }
        }

        if ( ImGui::CollapsingHeader( "Scene" ) )
        {
            if ( ImGui::DragInt( "Max Bounce Count", (int*)&m_Scene.m_MaxBounceCount, 0.5f, 0, m_Scene.s_MaxRayBounce ) )
            {
                m_IsFilmDirty = true;
            }

            static const char* s_FilterNames[] = { "Box", "Triangle", "Gaussian", "Mitchell", "Lanczos Sinc" };
            if ( ImGui::Combo( "Filter", (int*)&m_Scene.m_Filter, s_FilterNames, IM_ARRAYSIZE( s_FilterNames ) ) )
            {
                m_IsFilmDirty = true;
            }

            if ( ImGui::DragFloat( "Filter Radius", &m_Scene.m_FilterRadius, 0.1f, 0.001f, 16.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp ) )
            {
                m_IsFilmDirty = true;
            }

            if ( m_Scene.m_Filter == EFilter::Gaussian )
            {
                if ( ImGui::DragFloat( "Alpha", &m_Scene.m_GaussianFilterAlpha, 0.005f, 0.0f, 100.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp ) )
                {
                    m_IsFilmDirty = true;
                }
            }
            else if ( m_Scene.m_Filter == EFilter::Mitchell )
            {
                if ( ImGui::DragFloat( "B", &m_Scene.m_MitchellB, 0.01f, 0.0f, 100.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp ) )
                {
                    m_IsFilmDirty = true;
                }
                if ( ImGui::DragFloat( "C", &m_Scene.m_MitchellC, 0.01f, 0.0f, 100.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp ) )
                {
                    m_IsFilmDirty = true;
                }
            }
            else if ( m_Scene.m_Filter == EFilter::LanczosSinc )
            {
                if ( ImGui::DragInt( "Tau", (int*)&m_Scene.m_LanczosSincTau, 1, 1, 100, "%d", ImGuiSliderFlags_AlwaysClamp ) )
                {
                    m_IsFilmDirty = true;
                }
            }

            if ( ImGui::Checkbox( "GGX VNDF Sampling", &m_Scene.m_IsGGXVNDFSamplingEnabled ) )
            {
                m_PathTracer[ m_ActivePathTracerIndex ]->OnSceneLoaded();
                m_IsFilmDirty = true;
            }

            if ( ImGui::Checkbox( "Traverse BVH Front-to-back", &m_Scene.m_TraverseBVHFrontToBack ) )
            {
                m_PathTracer[ m_ActivePathTracerIndex ]->OnSceneLoaded();
                m_IsFilmDirty = true;
            }

            if ( ImGui::Checkbox( "Lights Visble to Camera", &m_Scene.m_IsLightVisible ) )
            {
                m_PathTracer[ m_ActivePathTracerIndex ]->OnSceneLoaded();
                m_IsFilmDirty = true;
            }
        }

        m_PathTracer[ m_ActivePathTracerIndex ]->OnImGUI();

        m_PostProcessing.OnImGUI();

        ImGui::PopItemWidth();
        ImGui::End();
    }

    {
        ImGui::Begin( "Scene", (bool*)0, ImGuiWindowFlags_MenuBar );

        if ( ImGui::BeginMenuBar() )
        {
            if ( ImGui::BeginMenu( "File" ) )
            {
                bool loadScene = ImGui::MenuItem( "Load Scene" );
                bool resetAndLoadScene = ImGui::MenuItem( "Reset & Load Scene" );
                if ( loadScene || resetAndLoadScene )
                {
                    OPENFILENAMEA ofn;
                    char filepath[ MAX_PATH ];
                    ZeroMemory( &ofn, sizeof( ofn ) );
                    ofn.lStructSize = sizeof( ofn );
                    ofn.hwndOwner = m_hWnd;
                    ofn.lpstrFile = filepath;
                    ofn.lpstrFile[ 0 ] = '\0';
                    ofn.nMaxFile = sizeof( filepath );
                    ofn.lpstrFilter = "Wavefront OBJ\0*.OBJ\0XML\0*.XML\0";
                    ofn.nFilterIndex = 1;
                    ofn.lpstrFileTitle = NULL;
                    ofn.nMaxFileTitle = 0;
                    ofn.lpstrInitialDir = NULL;
                    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

                    if ( GetOpenFileNameA( &ofn ) == TRUE )
                    {
                        LoadScene( filepath, resetAndLoadScene );
                    }
                }
                ImGui::EndMenu();
            }
            if ( ImGui::BeginMenu( "Edit", m_Scene.m_HasValidScene ) )
            {
                if ( ImGui::BeginMenu( "Create" ) )
                {
                    if ( ImGui::MenuItem( "Point Light", "", false, m_Scene.m_PointLights.size() < m_Scene.s_MaxLightsCount ) )
                    {
                        m_Scene.m_PointLights.emplace_back();
                        m_Scene.m_PointLights.back().color = XMFLOAT3( 1.0f, 1.0f, 1.0f );
                        m_Scene.m_PointLights.back().position = XMFLOAT3( 0.0f, 0.0f, 0.0f );
                        m_IsLightGPUBufferDirty = true;
                        m_IsFilmDirty = true;
                    }
                    if ( ImGui::MenuItem( "Environment Light", "", false, m_Scene.m_EnvironmentLight == nullptr ) )
                    {
                        m_Scene.m_EnvironmentLight = std::make_shared<SEnvironmentLight>();
                        m_Scene.m_EnvironmentLight->m_Color = XMFLOAT3( 1.0f, 1.0f, 1.0f );
                        m_IsLightGPUBufferDirty = true;
                        m_IsFilmDirty = true;
                    }
                    ImGui::EndMenu();
                }
                if ( ImGui::MenuItem( "Delete", "", false, m_Scene.m_ObjectSelection.m_PointLightSelectionIndex != -1 || m_Scene.m_ObjectSelection.m_IsEnvironmentLightSelected ) )
                {
                    if ( m_Scene.m_ObjectSelection.m_PointLightSelectionIndex != -1 )
                    {
                        m_Scene.m_PointLights.erase( m_Scene.m_PointLights.begin() + m_Scene.m_ObjectSelection.m_PointLightSelectionIndex );
                        m_Scene.m_ObjectSelection.m_PointLightSelectionIndex = -1;
                    }
                    else
                    {
                        bool hadEnvironmentTexture = m_Scene.m_EnvironmentLight->m_Texture != nullptr;
                        m_Scene.m_EnvironmentLight.reset();
                        if ( hadEnvironmentTexture )
                        {
                            // Allow the path tracer switching to a kernel without sampling the environment texture
                            m_PathTracer[ m_ActivePathTracerIndex ]->OnSceneLoaded();
                        }

                        m_Scene.m_ObjectSelection.m_IsEnvironmentLightSelected = false;
                    }
                    m_IsLightGPUBufferDirty = true;
                    m_IsFilmDirty = true;;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        if ( ImGui::CollapsingHeader( "Camera" ) )
        {
            bool isSelected = m_Scene.m_ObjectSelection.m_IsCameraSelected;
            if ( ImGui::Selectable( "Preview Camera", isSelected ) )
            {
                m_Scene.m_ObjectSelection.SelectCamera();
            }
        }

        if ( ImGui::CollapsingHeader( "Point Lights" ) )
        {
            char label[ 32 ];
            for ( size_t iLight = 0; iLight < m_Scene.m_PointLights.size(); ++iLight )
            {
                bool isSelected = ( iLight == m_Scene.m_ObjectSelection.m_PointLightSelectionIndex );
                sprintf( label, "Light %d", uint32_t( iLight ) );
                if ( ImGui::Selectable( label, isSelected ) )
                {
                    m_Scene.m_ObjectSelection.SelectPointLight( (int)iLight );
                }
            }
        }

        if ( ImGui::CollapsingHeader( "Environment Light" ) )
        {
            if ( m_Scene.m_EnvironmentLight )
            {
                if ( ImGui::Selectable( "Light##EnvLight", m_Scene.m_ObjectSelection.m_IsEnvironmentLightSelected ) )
                {
                    m_Scene.m_ObjectSelection.SelectEnvironmentLight();
                }
            }
        }

        if ( ImGui::CollapsingHeader( "Materials" ) )
        {
            for ( size_t iMaterial = 0; iMaterial < m_Scene.m_Materials.size(); ++iMaterial )
            {
                bool isSelected = ( iMaterial == m_Scene.m_ObjectSelection.m_MaterialSelectionIndex );
                ImGui::PushID( (int)iMaterial );
                if ( ImGui::Selectable( m_Scene.m_MaterialNames[ iMaterial ].c_str(), isSelected ) )
                {
                    m_Scene.m_ObjectSelection.SelectMaterial( (int)iMaterial );
                }
                ImGui::PopID();
            }
        }

        ImGui::End();
    }

    {
        ImGui::Begin( "Inspector" );

        ImGui::PushItemWidth( ImGui::GetFontSize() * -9 );

        if ( m_Scene.m_ObjectSelection.m_PointLightSelectionIndex >= 0 )
        {
            ImGui::SetColorEditOptions( ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR );
            if ( m_Scene.m_ObjectSelection.m_PointLightSelectionIndex < m_Scene.m_PointLights.size() )
            {
                SPointLight* selection = m_Scene.m_PointLights.data() + m_Scene.m_ObjectSelection.m_PointLightSelectionIndex;

                if ( ImGui::DragFloat3( "Position", (float*)&selection->position, 1.0f ) )
                    m_IsLightGPUBufferDirty = true;

                if ( ImGui::ColorEdit3( "Color", (float*)&selection->color ) )
                    m_IsLightGPUBufferDirty = true;
            }
        }
        else if ( m_Scene.m_ObjectSelection.m_IsEnvironmentLightSelected )
        {
            ImGui::SetColorEditOptions( ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR );
            if ( ImGui::ColorEdit3( "Radiance", (float*)&m_Scene.m_EnvironmentLight->m_Color ) )
                m_IsLightGPUBufferDirty = true;

            ImGui::InputText( "Image File", const_cast<char*>( m_Scene.m_EnvironmentLight->m_TextureFileName.c_str() ), m_Scene.m_EnvironmentLight->m_TextureFileName.size(), ImGuiInputTextFlags_ReadOnly );
            if ( ImGui::Button( "Browse##BrowseEnvImage" ) )
            {
                OPENFILENAMEA ofn;
                char filepath[ MAX_PATH ];
                ZeroMemory( &ofn, sizeof( ofn ) );
                ofn.lStructSize = sizeof( ofn );
                ofn.hwndOwner = m_hWnd;
                ofn.lpstrFile = filepath;
                ofn.lpstrFile[ 0 ] = '\0';
                ofn.nMaxFile = sizeof( filepath );
                ofn.lpstrFilter = "DDS\0*.DDS\0";
                ofn.nFilterIndex = 1;
                ofn.lpstrFileTitle = NULL;
                ofn.nMaxFileTitle = 0;
                ofn.lpstrInitialDir = NULL;
                ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

                if ( GetOpenFileNameA( &ofn ) == TRUE )
                {
                    m_Scene.m_EnvironmentLight->m_TextureFileName = filepath;
                    bool hasEnvTexturePreviously = m_Scene.m_EnvironmentLight->m_Texture.get() != nullptr;
                    m_Scene.m_EnvironmentLight->CreateTextureFromFile();
                    bool hasEnvTextureCurrently = m_Scene.m_EnvironmentLight->m_Texture.get() != nullptr;
                    if ( hasEnvTexturePreviously != hasEnvTextureCurrently )
                    {
                        m_PathTracer[ m_ActivePathTracerIndex ]->OnSceneLoaded();
                    }
                    m_IsFilmDirty = true;
                }
            }
            if ( m_Scene.m_EnvironmentLight->m_Texture )
            {
                ImGui::SameLine();
                if ( ImGui::Button( "Clear##ClearEnvImage" ) )
                {
                    m_Scene.m_EnvironmentLight->m_TextureFileName = "";
                    m_Scene.m_EnvironmentLight->m_Texture.reset();
                    m_PathTracer[ m_ActivePathTracerIndex ]->OnSceneLoaded();
                    m_IsFilmDirty = true;
                }
            }
        }
        else if ( m_Scene.m_ObjectSelection.m_MaterialSelectionIndex >= 0 )
        {
            if ( m_Scene.m_ObjectSelection.m_MaterialSelectionIndex < m_Scene.m_Materials.size() )
            {
                SMaterial* selection = m_Scene.m_Materials.data() + m_Scene.m_ObjectSelection.m_MaterialSelectionIndex;
                ImGui::SetColorEditOptions( ImGuiColorEditFlags_Float );
                if ( ImGui::ColorEdit3( "Albedo", (float*)&selection->m_Albedo ) )
                    m_IsMaterialGPUBufferDirty = true;
                ImGui::SetColorEditOptions( ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR );
                if ( ImGui::DragFloat( "Roughness", &selection->m_Roughness, 0.01f, 0.0f, 1.0f ) )
                    m_IsMaterialGPUBufferDirty = true;
                if ( ImGui::Checkbox( "Is Metal", &selection->m_IsMetal ) )
                {
                    if ( !selection->m_IsMetal )
                    {
                        // Reclamp IOR to above 1.0 when material is not metal
                        selection->m_IOR.x = std::max( 1.0f, selection->m_IOR.x );
                    }
                    m_IsMaterialGPUBufferDirty = true;
                }
                if ( selection->m_IsMetal )
                {
                    if ( ImGui::DragFloat3( "IOR", (float*)&selection->m_IOR, 0.01f, 0.0f, MAX_MATERIAL_IOR, "%.3f", ImGuiSliderFlags_AlwaysClamp ) )
                        m_IsMaterialGPUBufferDirty = true;
                    if ( ImGui::DragFloat3( "k", (float*)&selection->m_K, 0.01f, 0.0f, MAX_MATERIAL_K, "%.3f", ImGuiSliderFlags_AlwaysClamp ) )
                        m_IsMaterialGPUBufferDirty = true;
                }
                else
                {
                    if ( ImGui::DragFloat( "IOR", (float*)&selection->m_IOR, 0.01f, 1.0f, MAX_MATERIAL_IOR ) )
                        m_IsMaterialGPUBufferDirty = true;
                    if ( ImGui::DragFloat( "Transmission", &selection->m_Transmission, 0.01f, 0.0f, 1.0f ) )
                        m_IsMaterialGPUBufferDirty = true;
                }
                if ( ImGui::DragFloat2( "Texture Tiling", (float*)&selection->m_Tiling, 0.01f, 0.0f, 100000.0f ) )
                    m_IsMaterialGPUBufferDirty = true;
                if ( ImGui::Checkbox( "Albedo Texture", &selection->m_HasAlbedoTexture ) )
                    m_IsMaterialGPUBufferDirty = true;
                if ( ImGui::Checkbox( "Emission Texture", &selection->m_HasEmissionTexture ) )
                    m_IsMaterialGPUBufferDirty = true;
                if ( ImGui::Checkbox( "Roughness Texture", &selection->m_HasRoughnessTexture ) )
                    m_IsMaterialGPUBufferDirty = true;
            }
        }
        else if ( m_Scene.m_ObjectSelection.m_IsCameraSelected )
        {
            m_Scene.m_Camera.OnImGUI();

            if ( ImGui::InputFloat2( "Film Size", (float*)&m_Scene.m_FilmSize ) )
                m_IsFilmDirty = true;

            if ( ImGui::DragFloat( "Focal Length", (float*)&m_Scene.m_FocalLength, 0.000001f, 0.000001f, 1000.0f, "%.5f", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoRoundToFormat ) )
            {
                m_IsFilmDirty = true;
                if ( m_Scene.m_IsManualFilmDistanceEnabled )
                {
                    m_Scene.m_FocalDistance = m_Scene.CalculateFocalDistance();
                }
                else
                {
                    m_Scene.m_FilmDistanceNormalized = m_Scene.CalculateFilmDistanceNormalized();
                }
            }

            ImGui::Checkbox( "Manual Film Distance", &m_Scene.m_IsManualFilmDistanceEnabled );

            if ( m_Scene.m_IsManualFilmDistanceEnabled )
            {
                ImGui::LabelText( "Focal Distance", "%.5f", m_Scene.m_FocalDistance );

                if ( ImGui::DragFloat( "Film Distance Normalized", (float*)&m_Scene.m_FilmDistanceNormalized, 0.0001f, 0.000001f, 1.0f, "%.5f", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoRoundToFormat ) )
                {
                    m_IsFilmDirty = true;
                    m_Scene.m_FocalDistance = m_Scene.CalculateFocalDistance();
                }
            }
            else
            {
                if ( ImGui::DragFloat( "Focal Distance", (float*)&m_Scene.m_FocalDistance, 0.005f, 0.000001f, m_Scene.s_MaxFocalDistance, "%.5f", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoRoundToFormat ) )
                {
                    m_IsFilmDirty = true;
                    m_Scene.m_FilmDistanceNormalized = m_Scene.CalculateFilmDistanceNormalized();
                }

                ImGui::LabelText( "Film Distance Normalized", "%.5f", m_Scene.m_FilmDistanceNormalized );
            }

            if ( ImGui::DragFloat( "Aperture(f-number)", &m_Scene.m_RelativeAperture, 0.1f, 0.01f, 1000.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp ) )
                m_IsFilmDirty = true;

            if ( ImGui::DragInt( "Aperture Blade Count", (int*)&m_Scene.m_ApertureBladeCount, 1.0f, 2, 16, "%d", ImGuiSliderFlags_AlwaysClamp ) )
                m_IsFilmDirty = true;

            float apertureRotationDeg = DirectX::XMConvertToDegrees( m_Scene.m_ApertureRotation );
            if ( ImGui::DragFloat( "Aperture Rotation", &apertureRotationDeg, 1.0f, 0.0f, 360.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp ) )
            {
                m_Scene.m_ApertureRotation = DirectX::XMConvertToRadians( apertureRotationDeg );
                m_IsFilmDirty = true;
            }

            ImGui::DragFloat( "Shutter Time", &m_Scene.m_ShutterTime, 0.001f, 0.001f, 100000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp );
            ImGui::DragFloat( "ISO", &m_Scene.m_ISO, 50.f, 50.f, 100000.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp );
        }

        ImGui::End();
    }

    {
        ImGui::Begin( "Render Stats." );

        ImGui::Text( "Film Resolution: %dx%d", renderContext->m_CurrentResolutionWidth, renderContext->m_CurrentResolutionHeight );
        ImGui::Text( "Render Viewport: %dx%d", m_RenderViewport.m_Width, m_RenderViewport.m_Height );
        ImGui::Text( "Average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate );
        ImGui::Text( "SPP: %d", m_SPP );

        {
            POINT pos;
            ::GetCursorPos( &pos );
            ::ScreenToClient( m_hWnd, &pos );

            m_CursorPixelPosOnRenderViewport[ 0 ] = (uint32_t)std::clamp<int>( (int)pos.x - (int)m_RenderViewport.m_TopLeftX, 0, (int)m_RenderViewport.m_Width );
            m_CursorPixelPosOnRenderViewport[ 1 ] = (uint32_t)std::clamp<int>( (int)pos.y - (int)m_RenderViewport.m_TopLeftY, 0, (int)m_RenderViewport.m_Height );

            float filmPixelPerRenderViewportPixelX = (float)m_ResolutionWidth / m_RenderViewport.m_Width;
            float filmPixelPerRenderViewportPixelY = (float)m_ResolutionHeight / m_RenderViewport.m_Height;
            m_CursorPixelPosOnFilm[ 0 ] = (uint32_t)std::floorf( filmPixelPerRenderViewportPixelX * m_CursorPixelPosOnRenderViewport[ 0 ] );
            m_CursorPixelPosOnFilm[ 1 ] = (uint32_t)std::floorf( filmPixelPerRenderViewportPixelY * m_CursorPixelPosOnRenderViewport[ 1 ] );

            ImGui::Text( "Cursor Pos (Render Viewport): %d %d", m_CursorPixelPosOnRenderViewport[ 0 ], m_CursorPixelPosOnRenderViewport[ 1 ] );
            ImGui::Text( "Cursor Pos (Film): %d %d", m_CursorPixelPosOnFilm[ 0 ], m_CursorPixelPosOnFilm[ 1 ] );
        }

        ImGui::End();
    }

    if ( m_ShowRayTracingUI )
    {
        ImGui::Begin( "Ray Tracing Tool" );

        ImGui::InputInt2( "Pixel Position", (int*)m_RayTracingPixelPos );
        ImGui::DragFloat2( "Sub-pixel Position", (float*)m_RayTracingSubPixelPos, .1f, 0.f, .999999f, "%.6f", ImGuiSliderFlags_AlwaysClamp );
        if ( ImGui::Button( "Trace" ) )
        {
            DirectX::XMFLOAT2 screenPos = { (float)m_RayTracingPixelPos[ 0 ] + m_RayTracingSubPixelPos[ 0 ], (float)m_RayTracingPixelPos[ 1 ] + m_RayTracingSubPixelPos[ 1 ] };
            screenPos.x /= m_ResolutionWidth;
            screenPos.y /= m_ResolutionHeight;

            XMVECTOR rayOrigin, rayDirection;
            m_Scene.ScreenToCameraRay( screenPos, &rayOrigin, &rayDirection );
            m_RayTracingHasHit = m_Scene.TraceRay( rayOrigin, rayDirection, 0.f, &m_RayTracingHit );
        }

        if ( m_RayTracingHasHit )
        {
            SRayHit* hit = &m_RayTracingHit;
            char stringBuffer[ 512 ];
            sprintf_s( stringBuffer, ARRAY_LENGTH( stringBuffer ), "Found hit\nDistance: %f\nCoord: %f %f\nInstance: %d\nMesh index: %d\nMesh: %s\nTriangle: %d"
                , hit->m_T, hit->m_U, hit->m_V, hit->m_InstanceIndex, hit->m_MeshIndex, m_Scene.m_Meshes[ hit->m_MeshIndex ].GetName().c_str(), hit->m_TriangleIndex );
            ImGui::InputTextMultiline( "Result", stringBuffer, ARRAY_LENGTH( stringBuffer ), ImVec2( 0, 0 ), ImGuiInputTextFlags_ReadOnly );
        }
        else
        {
            char stringBuffer[] = "No hit";
            ImGui::InputText( "Result", stringBuffer, ARRAY_LENGTH( stringBuffer ), ImGuiInputTextFlags_ReadOnly );
        }

        ImGui::End();
    }

    {
        if ( !CMessagebox::GetSingleton().IsEmpty() )
        {
            CMessagebox::GetSingleton().OnImGUI();
        }
    }
}


