#include "stdafx.h"
#include "DirectComputeRayTracing.h"
#include "D3D12Adapter.h"
#include "D3D12Resource.h"
#include "D3D12DescriptorPoolHeap.h"
#include "CommandLineArgs.h"
#include "GPUTexture.h"
#include "GPUBuffer.h"
#include "Logging.h"
#include "StringConversion.h"
#include "Camera.h"
#include "PostProcessingRenderer.h"
#include "Timers.h"
#include "Rectangle.h"
#include "BxDFTexturesBuilding.h"
#include "RenderContext.h"
#include "MegakernelPathTracer.h"
#include "WavefrontPathTracer.h"
#include "Scene.h"
#include "MessageBox.h"
#include "ScopedRenderAnnotation.h"
#include "SampleConvolutionRenderer.h"
#include "Constants.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx12.h"
#include "imgui/imgui_impl_win32.h"
#include "ImGuiHelper.h"
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

    void UploadFrameConstantBuffer();

    void ClearFilmTexture();

    bool HandleFilmResolutionChange();

    void UpdateRenderViewport();

    void ResizeBackbuffer( uint32_t backbufferWidth, uint32_t backbufferHeight );

    void OnImGUI( SRenderContext* renderContext );

    HWND m_hWnd;

    uint32_t m_FrameSeed = 0;

    bool m_IsLightGPUBufferDirty;
    bool m_IsMaterialGPUBufferDirty;
    bool m_IsFilmDirty = true;
    bool m_IsLastFrameFilmDirty = true;

    SBxDFTextures m_BxDFTextures;
    std::vector<GPUTexturePtr> m_sRGBBackbuffers;
    std::vector<GPUTexturePtr> m_LinearBackbuffers;
    GPUBufferPtr m_RayTracingFrameConstantBuffer;

    CScene m_Scene;
    CPathTracer* m_PathTracer[ 2 ] = { nullptr, nullptr };
    uint32_t m_ActivePathTracerIndex = 0;
    CSampleConvolutionRenderer m_SampleConvolutionRenderer;
    PostProcessingRenderer m_PostProcessing;

    enum class EFrameSeedType { FrameIndex = 0, SampleCount = 1, Fixed = 2, _Count = 3 };
    EFrameSeedType m_FrameSeedType = EFrameSeedType::SampleCount;

    uint32_t m_NewResolutionWidth;
    uint32_t m_NewResolutionHeight;
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
    uint32_t padding[ 63 ];
};

static void AllocImGuiDescriptor( ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_desc_handle )
{
    CD3D12DescriptorPoolHeap* descriptorHeap = D3D12Adapter::GetDescriptorPoolHeap( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
    SD3D12DescriptorHandle CPUDescriptor = descriptorHeap->Allocate( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
    D3D12_GPU_DESCRIPTOR_HANDLE GPUDescriptor = descriptorHeap->CalculateGPUDescriptorHandle( CPUDescriptor );
    *out_cpu_desc_handle = CPUDescriptor.CPU;
    *out_gpu_desc_handle = GPUDescriptor;
}

static void FreeImGuiDescriptor( ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE gpu_desc_handle )
{
    CD3D12DescriptorPoolHeap* descriptorHeap = D3D12Adapter::GetDescriptorPoolHeap( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
    descriptorHeap->Free( cpu_desc_handle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
}

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

    ImGui_ImplDX12_InitInfo initInfo = {};
    initInfo.Device = D3D12Adapter::GetDevice();
    initInfo.CommandQueue = D3D12Adapter::GetCommandQueue();
    initInfo.NumFramesInFlight = D3D12Adapter::GetBackbufferCount();
    initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    initInfo.SrvDescriptorHeap = D3D12Adapter::GetDescriptorPoolHeap( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV )->GetD3DHeap();
    initInfo.SrvDescriptorAllocFn = AllocImGuiDescriptor;
    initInfo.SrvDescriptorFreeFn = FreeImGuiDescriptor;
    if ( !ImGui_ImplDX12_Init( &initInfo ) )
    {
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    return true;
}

static void ShutDownImGui()
{
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

static void ImGUINewFrame()
{
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

CDirectComputeRayTracing::CDirectComputeRayTracing( HWND hWnd )
{
    s_Renderer = new SRenderer( hWnd );
}

CDirectComputeRayTracing::~CDirectComputeRayTracing()
{
    D3D12Adapter::WaitForGPU();
    CD3D12Resource::FlushDeleteAll();

    delete s_Renderer;
    ShutDownImGui();
    D3D12Adapter::Destroy();
}

bool CDirectComputeRayTracing::Init()
{
    if ( !D3D12Adapter::Init( s_Renderer->m_hWnd ) )
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
        s_Renderer->UpdateRenderViewport();
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
    CD3D12Resource::CreateDeferredDeleteQueue();

    m_PathTracer[ 0 ] = new CMegakernelPathTracer( &m_Scene );
    m_PathTracer[ 1 ] = new CWavefrontPathTracer( &m_Scene );

    if ( !m_PathTracer[ m_ActivePathTracerIndex ]->Create() )
    {
        return false;
    }

    m_BxDFTextures = BxDFTexturesBuilding::Build();
    if ( !m_BxDFTextures.AllTexturesSet() )
    {
        return false;
    }

    m_RayTracingFrameConstantBuffer.reset( GPUBuffer::Create( 
          sizeof( RayTracingFrameConstants )
        , 0
        , DXGI_FORMAT_UNKNOWN
        , EGPUBufferUsage::Default
        , EGPUBufferBindFlag_ConstantBuffer ) );
    if ( !m_RayTracingFrameConstantBuffer )
        return false;

    m_sRGBBackbuffers.resize( D3D12Adapter::GetBackbufferCount() );
    m_LinearBackbuffers.resize( D3D12Adapter::GetBackbufferCount() );
    for ( uint32_t index = 0; index < D3D12Adapter::GetBackbufferCount(); ++index )
    { 
        m_sRGBBackbuffers[ index ].reset( GPUTexture::CreateFromSwapChain( DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, index ) );
        if ( !m_sRGBBackbuffers[ index ] )
        { 
            return false;
        }

        m_LinearBackbuffers[ index ].reset( GPUTexture::CreateFromSwapChain( index ) );
        if ( !m_LinearBackbuffers[ index ] )
        { 
            return false;
        }
    }

    if ( !m_SampleConvolutionRenderer.Init() )
        return false;

    if ( !m_PostProcessing.Init() )
        return false;

    LoadScene( CommandLineArgs::Singleton()->GetFilename().c_str(), true );

    ID3D12GraphicsCommandList* commandList = D3D12Adapter::GetCommandList();
    if ( FAILED( commandList->Close() ) )
    {
        return false;
    }
    ID3D12CommandList* commandLists[] = { commandList };
    D3D12Adapter::GetCommandQueue()->ExecuteCommandLists( 1, commandLists );
    D3D12Adapter::WaitForGPU();

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

    if ( !HandleFilmResolutionChange() )
    {
        return false;
    }

    m_NewResolutionWidth = m_Scene.m_ResolutionWidth;
    m_NewResolutionHeight = m_Scene.m_ResolutionHeight;

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

    renderContext->m_CurrentResolutionWidth = renderContext->m_IsSmallResolutionEnabled ? m_SmallResolutionWidth : m_Scene.m_ResolutionWidth;
    renderContext->m_CurrentResolutionRatio = (float)renderContext->m_CurrentResolutionWidth / m_Scene.m_ResolutionWidth;
    renderContext->m_CurrentResolutionHeight = renderContext->m_IsSmallResolutionEnabled ? m_SmallResolutionHeight : m_Scene.m_ResolutionHeight;

    if ( m_IsFilmDirty || renderContext->m_IsResolutionChanged )
    {
        // Transition the film texture to RTV
        {
            D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition( m_Scene.m_FilmTexture->GetTexture(),
                D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET );
            D3D12Adapter::GetCommandList()->ResourceBarrier( 1, &barrier );

            m_Scene.m_IsFilmTextureCleared = true;
        }

        ClearFilmTexture();

        if ( m_FrameSeedType == EFrameSeedType::SampleCount )
        {
            m_FrameSeed = 0;
        }

        m_SPP = 0;

        m_PathTracer[ m_ActivePathTracerIndex ]->ResetImage();
    }

    if ( m_IsLightGPUBufferDirty )
    {
        m_Scene.UpdateLightGPUData();
    }

    if ( m_IsMaterialGPUBufferDirty )
    {
        m_Scene.UpdateMaterialGPUData();
    }

    UploadFrameConstantBuffer();

    m_PathTracer[ m_ActivePathTracerIndex ]->Render( *renderContext, m_BxDFTextures );

    if ( m_PathTracer[ m_ActivePathTracerIndex ]->IsImageComplete() )
    {
        if ( m_FrameSeedType != EFrameSeedType::Fixed )
        {
            m_FrameSeed++;
        }

        ++m_SPP;
    }
    
    m_Scene.m_Camera.ClearDirty();
    m_IsLightGPUBufferDirty = false;
    m_IsMaterialGPUBufferDirty = false;
    m_IsFilmDirty = false;
}

void SRenderer::RenderOneFrame()
{
    m_FrameTimer.BeginFrame();

    SRenderContext renderContext;
    renderContext.m_EnablePostFX = true;
    renderContext.m_RayTracingFrameConstantBuffer = m_RayTracingFrameConstantBuffer;

    D3D12Adapter::BeginCurrentFrame();

    D3D12_VIEWPORT viewport;
    ID3D12GraphicsCommandList* commandList = D3D12Adapter::GetCommandList();
    SD3D12DescriptorHandle RTV;

    if ( m_Scene.m_HasValidScene )
    { 
        m_Scene.m_Camera.Update( m_FrameTimer.GetCurrentFrameDeltaTime() );

        DispatchRayTracing( &renderContext );

        if ( m_PathTracer[ m_ActivePathTracerIndex ]->IsImageComplete() || renderContext.m_IsSmallResolutionEnabled )
        {
            m_SampleConvolutionRenderer.Execute( renderContext, m_Scene );

            m_Scene.m_IsFilmTextureCleared = false;
            m_Scene.m_IsSampleTexturesRead = true;

            m_PostProcessing.ExecuteLuminanceCompute( m_Scene, renderContext );

            RTV = m_Scene.m_RenderResultTexture->GetRTV();
            commandList->OMSetRenderTargets( 1, &RTV.CPU, true, nullptr );

            viewport = { 0.0f, 0.0f, (float)m_Scene.m_ResolutionWidth, (float)m_Scene.m_ResolutionHeight, 0.0f, 1.0f };
            commandList->RSSetViewports( 1, &viewport );

            m_PostProcessing.ExecutePostFX( renderContext, m_Scene );

            m_Scene.m_IsRenderResultTextureRead = false;
        }
    }

    const uint32_t backbufferIndex = D3D12Adapter::GetBackbufferIndex();

    // Transition the current backbuffer
    {
        D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition( m_sRGBBackbuffers[ backbufferIndex ]->GetTexture(),
            D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET );
        commandList->ResourceBarrier( 1, &barrier );
    }

    RTV = m_sRGBBackbuffers[ backbufferIndex ]->GetRTV();
    commandList->OMSetRenderTargets( 1, &RTV.CPU, true, nullptr );

    DXGI_SWAP_CHAIN_DESC swapChainDesc;
    D3D12Adapter::GetSwapChain()->GetDesc( &swapChainDesc );
    viewport = { 0.0f, 0.0f, (float)swapChainDesc.BufferDesc.Width, (float)swapChainDesc.BufferDesc.Height, 0.0f, 1.0f };
    commandList->RSSetViewports( 1, &viewport );

    XMFLOAT4 clearColor = { 0.0f, 0.0f, 0.0f, 0.0f };
    commandList->ClearRenderTargetView( RTV.CPU, (float*)&clearColor, 0, nullptr );

    if ( m_Scene.m_HasValidScene )
    {
        viewport = { (float)m_RenderViewport.m_TopLeftX, (float)m_RenderViewport.m_TopLeftY, (float)m_RenderViewport.m_Width, (float)m_RenderViewport.m_Height, 0.0f, 1.0f };
        commandList->RSSetViewports( 1, &viewport );

        m_PostProcessing.ExecuteCopy( m_Scene );
        m_Scene.m_IsRenderResultTextureRead = true;
    }

    ImGUINewFrame();
    OnImGUI( &renderContext );
    ImGui::Render();

    RTV = m_LinearBackbuffers[ backbufferIndex ]->GetRTV();
    commandList->OMSetRenderTargets( 1, &RTV.CPU, true, nullptr );

    {
        SCOPED_RENDER_ANNOTATION( commandList, L"ImGUI" );
        ImGui_ImplDX12_RenderDrawData( ImGui::GetDrawData(), commandList );
    }

    commandList->OMSetRenderTargets( 0, nullptr, true, nullptr );

    // Transition the current backbuffer
    {
        D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition( m_sRGBBackbuffers[ backbufferIndex ]->GetTexture(),
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT );
        commandList->ResourceBarrier( 1, &barrier );
    }

    HRESULT hr = commandList->Close();
    if ( FAILED( hr ) )
    {
        LOG_STRING_FORMAT( "CommandList close failure: %x\n", hr );
    }

    ID3D12CommandList* commandLists[] = { commandList };
    D3D12Adapter::GetCommandQueue()->ExecuteCommandLists( 1, commandLists );
    D3D12Adapter::Present( 0 );
    D3D12Adapter::MoveToNextFrame();

    CD3D12Resource::FlushDelete();

    // State decay to common state
    m_Scene.m_IsLightBufferRead = true;
    m_Scene.m_IsMaterialBufferRead = true;
}

void SRenderer::UploadFrameConstantBuffer()
{
    GPUBuffer::SUploadContext context = {};
    if ( m_RayTracingFrameConstantBuffer->AllocateUploadContext( &context ) )
    {
        RayTracingFrameConstants* constants = (RayTracingFrameConstants*)context.Map();
        if ( constants )
        {
            constants->frameSeed = m_FrameSeed;
            context.Unmap();
            context.Upload(); // No barrier needed because of implicit state promotion
        }
    }
}

void SRenderer::ClearFilmTexture()
{
    ID3D12GraphicsCommandList* commandList = D3D12Adapter::GetCommandList();
    const float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    commandList->ClearRenderTargetView( m_Scene.m_FilmTexture->GetRTV().CPU, clearColor, 0, nullptr );
}

bool SRenderer::HandleFilmResolutionChange()
{
    if ( !m_PostProcessing.OnFilmResolutionChange( m_Scene.m_ResolutionWidth, m_Scene.m_ResolutionHeight ) )
    {
        return false;
    }

    UpdateRenderViewport();

    // Aspect ratio might change due to rounding error, but this is neglectable
    m_SmallResolutionWidth = std::max( 1u, (uint32_t)std::roundf( m_Scene.m_ResolutionWidth * 0.25f ) );
    m_SmallResolutionHeight = std::max( 1u, (uint32_t)std::roundf( m_Scene.m_ResolutionHeight * 0.25f ) );

    return true;
}

void SRenderer::UpdateRenderViewport()
{
    DXGI_SWAP_CHAIN_DESC swapChainDesc;
    D3D12Adapter::GetSwapChain()->GetDesc( &swapChainDesc );

    uint32_t renderWidth = m_Scene.m_ResolutionWidth;
    uint32_t renderHeight = m_Scene.m_ResolutionHeight;
    float scale = (float)swapChainDesc.BufferDesc.Width / renderWidth;
    float desiredViewportHeight = renderHeight * scale;
    if ( desiredViewportHeight > swapChainDesc.BufferDesc.Height )
    {
        scale = (float)swapChainDesc.BufferDesc.Height / renderHeight;
    }

    m_RenderViewport.m_Width = uint32_t( renderWidth * scale );
    m_RenderViewport.m_Height = uint32_t( renderHeight * scale );
    m_RenderViewport.m_TopLeftX = uint32_t( (swapChainDesc.BufferDesc.Width - m_RenderViewport.m_Width ) * 0.5f );
    m_RenderViewport.m_TopLeftY = uint32_t( (swapChainDesc.BufferDesc.Height - m_RenderViewport.m_Height ) * 0.5f );
}

void SRenderer::ResizeBackbuffer( uint32_t backbufferWidth, uint32_t backbufferHeight )
{
    D3D12Adapter::WaitForGPU();

    for ( GPUTexturePtr& backbuffer : m_sRGBBackbuffers )
    {
        backbuffer.reset();
    }
    for ( GPUTexturePtr& backbuffer : m_LinearBackbuffers )
    {
        backbuffer.reset();
    }

    D3D12Adapter::ResizeSwapChainBuffers( backbufferWidth, backbufferHeight );

    m_sRGBBackbuffers.resize( D3D12Adapter::GetBackbufferCount() );
    m_LinearBackbuffers.resize( D3D12Adapter::GetBackbufferCount() );
    for ( uint32_t index = 0; index < D3D12Adapter::GetBackbufferCount(); ++index )
    {
        m_sRGBBackbuffers[ index ].reset( GPUTexture::CreateFromSwapChain( DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, index ) );
        m_LinearBackbuffers[ index ].reset( GPUTexture::CreateFromSwapChain( index ) );
    }
}

void SRenderer::OnImGUI( SRenderContext* renderContext )
{
    if ( ImGui::IsKeyPressed( ImGuiKey_F1, false ) )
    {
        m_ShowUI = !m_ShowUI;
    }
    if ( ImGui::IsKeyPressed( ImGuiKey_F2, false ) )
    {
        m_ShowRayTracingUI = !m_ShowRayTracingUI;
    }

    if ( !m_ShowUI )
        return;

    if ( m_Scene.m_HasValidScene )
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

            ImGui::DragInt( "Resolution Width", (int*)&m_NewResolutionWidth, 16, 16, 4096, "%d", ImGuiSliderFlags_AlwaysClamp );
            ImGui::DragInt( "Resolution Height", (int*)&m_NewResolutionHeight, 16, 16, 4096, "%d", ImGuiSliderFlags_AlwaysClamp );
            if ( m_Scene.m_ResolutionWidth != m_NewResolutionWidth || m_Scene.m_ResolutionHeight != m_NewResolutionHeight )
            {
                if ( ImGui::Button( "Apply##ApplyResolutionChange" ) )
                {
                    m_Scene.m_ResolutionWidth = m_NewResolutionWidth;
                    m_Scene.m_ResolutionHeight = m_NewResolutionHeight;
                    m_Scene.RecreateFilmTextures();
                    HandleFilmResolutionChange();
                    m_IsFilmDirty = true;
                }
            }

            ImGui::DragInt( "Small Resolution Width", (int*)&m_SmallResolutionWidth, 16, 16, m_Scene.m_ResolutionWidth, "%d", ImGuiSliderFlags_AlwaysClamp );
            ImGui::DragInt( "Small Resolution Height", (int*)&m_SmallResolutionHeight, 16, 16, m_Scene.m_ResolutionHeight, "%d", ImGuiSliderFlags_AlwaysClamp );

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
                    ofn.lpstrFilter = "All Scene Files (*.obj;*.xml)\0*.OBJ;*.XML\0";
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
                    if ( ImGui::MenuItem( "Point Light", "", false, m_Scene.GetLightCount() < m_Scene.s_MaxLightsCount ) )
                    {
                        m_Scene.m_PunctualLights.emplace_back();
                        SPunctualLight& newLight = m_Scene.m_PunctualLights.back();
                        newLight.m_Color = XMFLOAT3( 1.0f, 1.0f, 1.0f );
                        newLight.m_Position = XMFLOAT3( 0.0f, 0.0f, 0.0f );
                        newLight.m_IsDirectionalLight = false;
                        m_IsLightGPUBufferDirty = true;
                        m_IsFilmDirty = true;
                    }
                    if ( ImGui::MenuItem( "Directional Light", "", false, m_Scene.GetLightCount() < m_Scene.s_MaxLightsCount ) )
                    {
                        m_Scene.m_PunctualLights.emplace_back();
                        SPunctualLight& newLight = m_Scene.m_PunctualLights.back();
                        newLight.m_Color = XMFLOAT3( 1.0f, 1.0f, 1.0f );
                        newLight.SetEulerAnglesFromDirection( XMFLOAT3( 0.f, -1.f, 0.f ) );
                        newLight.m_IsDirectionalLight = true;
                        m_IsLightGPUBufferDirty = true;
                        m_IsFilmDirty = true;
                    }
                    if ( ImGui::MenuItem( "Environment Light", "", false, m_Scene.m_EnvironmentLight == nullptr && m_Scene.GetLightCount() < m_Scene.s_MaxLightsCount ) )
                    {
                        m_Scene.m_EnvironmentLight = std::make_shared<SEnvironmentLight>();
                        m_Scene.m_EnvironmentLight->m_Color = XMFLOAT3( 1.0f, 1.0f, 1.0f );
                        m_IsLightGPUBufferDirty = true;
                        m_IsFilmDirty = true;
                    }
                    ImGui::EndMenu();
                }
                if ( ImGui::MenuItem( "Delete", "", false, m_Scene.m_ObjectSelection.m_PunctualLightSelectionIndex != -1 || m_Scene.m_ObjectSelection.m_IsEnvironmentLightSelected ) )
                {
                    if ( m_Scene.m_ObjectSelection.m_PunctualLightSelectionIndex != -1 )
                    {
                        m_Scene.m_PunctualLights.erase( m_Scene.m_PunctualLights.begin() + m_Scene.m_ObjectSelection.m_PunctualLightSelectionIndex );
                        m_Scene.m_ObjectSelection.m_PunctualLightSelectionIndex = -1;
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

        if ( ImGui::CollapsingHeader( "Punctual Lights" ) )
        {
            char label[ 32 ];
            for ( size_t iLight = 0; iLight < m_Scene.m_PunctualLights.size(); ++iLight )
            {
                bool isSelected = ( iLight == m_Scene.m_ObjectSelection.m_PunctualLightSelectionIndex );
                sprintf( label, "Light %d", uint32_t( iLight ) );
                if ( ImGui::Selectable( label, isSelected ) )
                {
                    m_Scene.m_ObjectSelection.SelectPunctualLight( (int)iLight );
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

        if ( m_Scene.m_ObjectSelection.m_PunctualLightSelectionIndex >= 0 )
        {
            ImGui::SetColorEditOptions( ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR );
            if ( m_Scene.m_ObjectSelection.m_PunctualLightSelectionIndex < m_Scene.m_PunctualLights.size() )
            {
                SPunctualLight* selection = m_Scene.m_PunctualLights.data() + m_Scene.m_ObjectSelection.m_PunctualLightSelectionIndex;

                if ( selection->m_IsDirectionalLight )
                {
                    if ( ImGuiHelper::DragFloat3RadianInDegree( "Euler Angles", (float*)&selection->m_EulerAngles, 1.f ) )
                        m_IsLightGPUBufferDirty = true;

                    XMFLOAT3 direction = selection->CalculateDirection();
                    ImGui::LabelText( "Direction", "%.3f, %.3f, %.3f", direction.x, direction.y, direction.z );
                }
                else
                {
                    if ( ImGui::DragFloat3( "Position", (float*)&selection->m_Position, 1.0f ) )
                        m_IsLightGPUBufferDirty = true;
                }

                if ( ImGui::ColorEdit3( "Color", (float*)&selection->m_Color ) )
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

                static const char* s_MaterialTypeNames[] = { "Diffuse", "Plastic", "Conductor", "Dielectric" };
                if ( ImGui::Combo( "Type", (int*)&selection->m_MaterialType, s_MaterialTypeNames, IM_ARRAYSIZE( s_MaterialTypeNames ) ) )
                {
                    if ( selection->m_MaterialType != EMaterialType::Conductor )
                    {
                        // Reclamp IOR to above 1.0 when material is not conductor
                        selection->m_IOR.x = std::max( 1.0f, selection->m_IOR.x );
                    }
                    m_IsMaterialGPUBufferDirty = true;
                }

                if ( selection->m_MaterialType == EMaterialType::Diffuse || selection->m_MaterialType == EMaterialType::Plastic )
                {
                    ImGui::SetColorEditOptions( ImGuiColorEditFlags_Float );
                    m_IsMaterialGPUBufferDirty |= ImGui::ColorEdit3( "Albedo", (float*)&selection->m_Albedo );
                    m_IsMaterialGPUBufferDirty |= ImGui::Checkbox( "Albedo Texture", &selection->m_HasAlbedoTexture );
                }

                if ( selection->m_MaterialType != EMaterialType::Diffuse )
                {
                    ImGui::SetColorEditOptions( ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR );
                    m_IsMaterialGPUBufferDirty |= ImGui::DragFloat( "Roughness", &selection->m_Roughness, 0.01f, 0.0f, 1.0f );
                    m_IsMaterialGPUBufferDirty |= ImGui::Checkbox( "Roughness Texture", &selection->m_HasRoughnessTexture );
                    m_IsMaterialGPUBufferDirty |= ImGui::Checkbox( "Multiscattering", &selection->m_Multiscattering );
                }

                if ( selection->m_MaterialType == EMaterialType::Conductor )
                {
                    m_IsMaterialGPUBufferDirty |= ImGui::DragFloat3( "eta", (float*)&selection->m_IOR, 0.01f, 0.0f, MAX_MATERIAL_ETA, "%.3f", ImGuiSliderFlags_AlwaysClamp );              
                    m_IsMaterialGPUBufferDirty |= ImGui::DragFloat3( "k", (float*)&selection->m_K, 0.01f, 0.0f, MAX_MATERIAL_K, "%.3f", ImGuiSliderFlags_AlwaysClamp );
                }
                else if ( selection->m_MaterialType != EMaterialType::Diffuse )
                {
                    m_IsMaterialGPUBufferDirty |= ImGui::DragFloat( "IOR", (float*)&selection->m_IOR, 0.01f, 1.0f, MAX_MATERIAL_IOR, "%.3f", ImGuiSliderFlags_AlwaysClamp );
                }

                if ( selection->m_MaterialType != EMaterialType::Dielectric )
                {
                    m_IsMaterialGPUBufferDirty |= ImGui::Checkbox( "Two Sided", &selection->m_IsTwoSided );
                }

                m_IsMaterialGPUBufferDirty |= ImGui::DragFloat2( "Texture Tiling", (float*)&selection->m_Tiling, 0.01f, 0.0f, 100000.0f );
            }
        }
        else if ( m_Scene.m_ObjectSelection.m_IsCameraSelected )
        {
            m_Scene.m_Camera.OnImGUI();

            if ( ImGui::DragFloat( "Film Width", &m_Scene.m_FilmSize.x, 0.005f, 0.001f, 999.f, "%.3f", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoRoundToFormat ) )
            {
                m_Scene.m_FilmSize.y = m_Scene.m_FilmSize.x / m_Scene.m_ResolutionWidth * m_Scene.m_ResolutionHeight;
                m_IsFilmDirty = true;
            }
            if ( ImGui::DragFloat( "Film Height", &m_Scene.m_FilmSize.y, 0.005f, 0.001f, 999.f, "%.3f", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoRoundToFormat ) )
            {
                m_Scene.m_FilmSize.x = m_Scene.m_FilmSize.y / m_Scene.m_ResolutionHeight * m_Scene.m_ResolutionWidth;
                m_IsFilmDirty = true;
            }

            static const char* s_CameraTypeNames[] = { "PinHole", "ThinLens" };
            if ( ImGui::Combo( "Type", (int*)&m_Scene.m_CameraType, s_CameraTypeNames, IM_ARRAYSIZE( s_CameraTypeNames ) ) )
                m_IsFilmDirty = true;
            
            if ( m_Scene.m_CameraType == ECameraType::PinHole )
            {
                float fovDeg = DirectX::XMConvertToDegrees( m_Scene.m_FoVX );
                if ( ImGui::DragFloat( "FoV", (float*)&fovDeg, 1.f, 0.00001f, 179.9f, "%.2f", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoRoundToFormat ) )
                { 
                    m_Scene.m_FoVX = DirectX::XMConvertToRadians( fovDeg );
                    m_IsFilmDirty = true;
                }
            }
            else
            { 
                if ( ImGui::DragFloat( "Focal Length", (float*)&m_Scene.m_FocalLength, 0.000001f, 0.000001f, 1000.0f, "%.5f", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoRoundToFormat ) )
                    m_IsFilmDirty = true;
            
                if ( ImGui::DragFloat( "Focal Distance", (float*)&m_Scene.m_FocalDistance, 0.005f, 0.000001f, m_Scene.s_MaxFocalDistance, "%.5f", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoRoundToFormat ) )
                    m_IsFilmDirty = true;

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
            }

            ImGui::DragFloat( "Shutter Time", &m_Scene.m_ShutterTime, 0.001f, 0.001f, 100000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp );
            ImGui::DragFloat( "ISO", &m_Scene.m_ISO, 50.f, 50.f, 100000.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp );
        }

        ImGui::End();
    }

    if ( m_Scene.m_HasValidScene )
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

            float filmPixelPerRenderViewportPixelX = (float)m_Scene.m_ResolutionWidth / m_RenderViewport.m_Width;
            float filmPixelPerRenderViewportPixelY = (float)m_Scene.m_ResolutionHeight / m_RenderViewport.m_Height;
            m_CursorPixelPosOnFilm[ 0 ] = (uint32_t)std::floorf( filmPixelPerRenderViewportPixelX * m_CursorPixelPosOnRenderViewport[ 0 ] );
            m_CursorPixelPosOnFilm[ 1 ] = (uint32_t)std::floorf( filmPixelPerRenderViewportPixelY * m_CursorPixelPosOnRenderViewport[ 1 ] );

            ImGui::Text( "Cursor Pos (Render Viewport): %d %d", m_CursorPixelPosOnRenderViewport[ 0 ], m_CursorPixelPosOnRenderViewport[ 1 ] );
            ImGui::Text( "Cursor Pos (Film): %d %d", m_CursorPixelPosOnFilm[ 0 ], m_CursorPixelPosOnFilm[ 1 ] );
        }

        ImGui::End();
    }

    if ( m_ShowRayTracingUI && m_Scene.m_HasValidScene )
    {
        ImGui::Begin( "Ray Tracing Tool" );

        ImGui::InputInt2( "Pixel Position", (int*)m_RayTracingPixelPos );
        ImGui::DragFloat2( "Sub-pixel Position", (float*)m_RayTracingSubPixelPos, .1f, 0.f, .999999f, "%.6f", ImGuiSliderFlags_AlwaysClamp );
        if ( ImGui::Button( "Trace" ) )
        {
            DirectX::XMFLOAT2 screenPos = { (float)m_RayTracingPixelPos[ 0 ] + m_RayTracingSubPixelPos[ 0 ], (float)m_RayTracingPixelPos[ 1 ] + m_RayTracingSubPixelPos[ 1 ] };
            screenPos.x /= m_Scene.m_ResolutionWidth;
            screenPos.y /= m_Scene.m_ResolutionHeight;

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


