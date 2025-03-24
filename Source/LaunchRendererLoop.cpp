#include "stdafx.h"
#include "DirectComputeRayTracing.h"
#include "D3D12Adapter.h"
#include "D3D12Resource.h"
#include "CommandLineArgs.h"
#include "GPUTexture.h"
#include "GPUBuffer.h"
#include "Logging.h"
#include "Camera.h"
#include "RenderContext.h"
#include "MegakernelPathTracer.h"
#include "WavefrontPathTracer.h"
#include "ScopedRenderAnnotation.h"

using namespace DirectX;
using SRenderer = CDirectComputeRayTracing::SRenderer;

struct alignas( 256 ) RayTracingFrameConstants
{
    uint32_t frameSeed;
};

CDirectComputeRayTracing::CDirectComputeRayTracing( HWND hWnd )
{
    m_Renderer = new SRenderer();
    m_Renderer->m_hWnd = hWnd;
}

CDirectComputeRayTracing::~CDirectComputeRayTracing()
{
    D3D12Adapter::WaitForGPU( false ); // Wait for the latest frame (the last frame) to finish

    m_Renderer->ShutdownImGui();
    delete m_Renderer;

    CD3D12Resource::FlushDeleteAll();
    D3D12Adapter::Destroy();
}

bool SRenderer::Init()
{
    if ( !D3D12Adapter::Init( m_hWnd ) )
        return false;

    if ( !InitImGui( m_hWnd ) )
        return false;

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

    if ( !InitSampleConvolution() )
        return false;

    if ( !InitPostProcessing() )
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

SRenderer::~SRenderer()
{
    m_PathTracer[ m_ActivePathTracerIndex ]->Destroy();
    for ( auto& it : m_PathTracer )
    {
        delete it;
    }
}

static void UpdateRenderViewport( SRenderer* r )
{
    DXGI_SWAP_CHAIN_DESC swapChainDesc;
    D3D12Adapter::GetSwapChain()->GetDesc( &swapChainDesc );

    uint32_t renderWidth = r->m_Scene.m_ResolutionWidth;
    uint32_t renderHeight = r->m_Scene.m_ResolutionHeight;
    float scale = (float)swapChainDesc.BufferDesc.Width / renderWidth;
    float desiredViewportHeight = renderHeight * scale;
    if ( desiredViewportHeight > swapChainDesc.BufferDesc.Height )
    {
        scale = (float)swapChainDesc.BufferDesc.Height / renderHeight;
    }

    r->m_RenderViewport.m_Width = uint32_t( renderWidth * scale );
    r->m_RenderViewport.m_Height = uint32_t( renderHeight * scale );
    r->m_RenderViewport.m_TopLeftX = uint32_t( (swapChainDesc.BufferDesc.Width - r->m_RenderViewport.m_Width ) * 0.5f );
    r->m_RenderViewport.m_TopLeftY = uint32_t( (swapChainDesc.BufferDesc.Height - r->m_RenderViewport.m_Height ) * 0.5f );
}

static void ResizeBackbuffer( SRenderer* r, uint32_t backbufferWidth, uint32_t backbufferHeight )
{
    D3D12Adapter::WaitForGPU( false ); // Wait for the latest frame (the last frame) to finish

    for ( GPUTexturePtr& backbuffer : r->m_sRGBBackbuffers )
    {
        backbuffer.reset();
    }
    for ( GPUTexturePtr& backbuffer : r->m_LinearBackbuffers )
    {
        backbuffer.reset();
    }

    D3D12Adapter::ResizeSwapChainBuffers( backbufferWidth, backbufferHeight );

    r->m_sRGBBackbuffers.resize( D3D12Adapter::GetBackbufferCount() );
    r->m_LinearBackbuffers.resize( D3D12Adapter::GetBackbufferCount() );
    for ( uint32_t index = 0; index < D3D12Adapter::GetBackbufferCount(); ++index )
    {
        r->m_sRGBBackbuffers[ index ].reset( GPUTexture::CreateFromSwapChain( DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, index ) );
        r->m_LinearBackbuffers[ index ].reset( GPUTexture::CreateFromSwapChain( index ) );
    }
}

bool SRenderer::OnWndMessage( UINT message, WPARAM wParam, LPARAM lParam )
{
    if ( ProcessImGuiWindowMessage( m_hWnd, message, wParam, lParam ) )
        return true;

    if ( message == WM_SIZE )
    {
        UINT width = LOWORD( lParam );
        UINT height = HIWORD( lParam );
        ResizeBackbuffer( this, width, height );
        UpdateRenderViewport( this );
    }

    return m_Scene.m_Camera.OnWndMessage( message, wParam, lParam );
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

static void UploadFrameConstantBuffer( SRenderer* r )
{
    GPUBuffer::SUploadContext context = {};
    if ( r->m_RayTracingFrameConstantBuffer->AllocateUploadContext( &context ) )
    {
        RayTracingFrameConstants* constants = (RayTracingFrameConstants*)context.Map();
        if ( constants )
        {
            constants->frameSeed = r->m_FrameSeed;
            context.Unmap();
            context.Upload(); // No barrier needed because of implicit state promotion
        }
    }
}

static void ClearFilmTexture( SRenderer* r )
{
    ID3D12GraphicsCommandList* commandList = D3D12Adapter::GetCommandList();
    const float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    commandList->ClearRenderTargetView( r->m_Scene.m_FilmTexture->GetRTV().CPU, clearColor, 0, nullptr );
}

static void DispatchRayTracing( SRenderer* r, SRenderContext* renderContext )
{
    r->m_IsFilmDirty = r->m_IsFilmDirty || r->m_IsLightGPUBufferDirty || r->m_IsMaterialGPUBufferDirty || r->m_Scene.m_Camera.IsDirty() || r->m_PathTracer[ r->m_ActivePathTracerIndex ]->AcquireFilmClearTrigger();

    renderContext->m_IsResolutionChanged = ( r->m_IsFilmDirty != r->m_IsLastFrameFilmDirty );
    renderContext->m_IsSmallResolutionEnabled = r->m_IsFilmDirty;

    r->m_IsLastFrameFilmDirty = r->m_IsFilmDirty;

    renderContext->m_CurrentResolutionWidth = renderContext->m_IsSmallResolutionEnabled ? r->m_SmallResolutionWidth : r->m_Scene.m_ResolutionWidth;
    renderContext->m_CurrentResolutionRatio = (float)renderContext->m_CurrentResolutionWidth / r->m_Scene.m_ResolutionWidth;
    renderContext->m_CurrentResolutionHeight = renderContext->m_IsSmallResolutionEnabled ? r->m_SmallResolutionHeight : r->m_Scene.m_ResolutionHeight;

    if ( r->m_IsFilmDirty || renderContext->m_IsResolutionChanged )
    {
        // Transition the film texture to RTV
        if ( r->m_Scene.m_FilmTextureStates != D3D12_RESOURCE_STATE_RENDER_TARGET )
        {
            D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition( r->m_Scene.m_FilmTexture->GetTexture(),
                r->m_Scene.m_FilmTextureStates, D3D12_RESOURCE_STATE_RENDER_TARGET );
            D3D12Adapter::GetCommandList()->ResourceBarrier( 1, &barrier );

            r->m_Scene.m_FilmTextureStates = D3D12_RESOURCE_STATE_RENDER_TARGET;
        }

        ClearFilmTexture( r );

        if ( r->m_FrameSeedType == SRenderer::EFrameSeedType::SampleCount )
        {
            r->m_FrameSeed = 0;
        }

        r->m_SPP = 0;

        r->m_PathTracer[ r->m_ActivePathTracerIndex ]->ResetImage();
    }

    if ( r->m_IsLightGPUBufferDirty )
    {
        r->m_Scene.UpdateLightGPUData();
    }

    if ( r->m_IsMaterialGPUBufferDirty )
    {
        r->m_Scene.UpdateMaterialGPUData();
    }

    UploadFrameConstantBuffer( r );

    r->m_PathTracer[ r->m_ActivePathTracerIndex ]->Render( *renderContext, r->m_BxDFTextures );

    if ( r->m_PathTracer[ r->m_ActivePathTracerIndex ]->IsImageComplete() )
    {
        if ( r->m_FrameSeedType != SRenderer::EFrameSeedType::Fixed )
        {
            r->m_FrameSeed++;
        }

        ++r->m_SPP;
    }
    
    r->m_Scene.m_Camera.ClearDirty();
    r->m_IsLightGPUBufferDirty = false;
    r->m_IsMaterialGPUBufferDirty = false;
    r->m_IsFilmDirty = false;
}

void SRenderer::RenderOneFrame()
{
    m_FrameTimer.BeginFrame();

    SRenderContext renderContext;
    renderContext.m_EnablePostFX = true;
    renderContext.m_RayTracingFrameConstantBuffer = m_RayTracingFrameConstantBuffer;

    D3D12Adapter::BeginCurrentFrame();

    D3D12_VIEWPORT viewport;
    D3D12_RECT scissorRect;
    ID3D12GraphicsCommandList* commandList = D3D12Adapter::GetCommandList();
    SD3D12DescriptorHandle RTV;

    if ( m_Scene.m_HasValidScene )
    { 
        m_Scene.m_Camera.Update( m_FrameTimer.GetCurrentFrameDeltaTime() );

        DispatchRayTracing( this, &renderContext );

        if ( m_PathTracer[ m_ActivePathTracerIndex ]->IsImageComplete() || renderContext.m_IsSmallResolutionEnabled )
        {
            ExecuteSampleConvolution( renderContext );

            DispatchSceneLuminanceCompute( renderContext );

            RTV = m_Scene.m_RenderResultTexture->GetRTV();
            commandList->OMSetRenderTargets( 1, &RTV.CPU, true, nullptr );

            viewport = { 0.0f, 0.0f, (float)m_Scene.m_ResolutionWidth, (float)m_Scene.m_ResolutionHeight, 0.0f, 1.0f };
            scissorRect = CD3DX12_RECT( 0, 0, m_Scene.m_ResolutionWidth, m_Scene.m_ResolutionHeight );
            commandList->RSSetViewports( 1, &viewport );
            commandList->RSSetScissorRects( 1, &scissorRect );

            ExecutePostProcessing( renderContext );
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
    scissorRect = CD3DX12_RECT( 0, 0, swapChainDesc.BufferDesc.Width, swapChainDesc.BufferDesc.Height );
    commandList->RSSetViewports( 1, &viewport );
    commandList->RSSetScissorRects( 1, &scissorRect );

    XMFLOAT4 clearColor = { 0.0f, 0.0f, 0.0f, 0.0f };
    commandList->ClearRenderTargetView( RTV.CPU, (float*)&clearColor, 0, nullptr );

    if ( m_Scene.m_HasValidScene )
    {
        viewport = { (float)m_RenderViewport.m_TopLeftX, (float)m_RenderViewport.m_TopLeftY, (float)m_RenderViewport.m_Width, (float)m_RenderViewport.m_Height, 0.0f, 1.0f };
        scissorRect = CD3DX12_RECT( m_RenderViewport.m_TopLeftX, m_RenderViewport.m_TopLeftY,
            m_RenderViewport.m_TopLeftX + m_RenderViewport.m_Width, m_RenderViewport.m_TopLeftY + m_RenderViewport.m_Height );
        commandList->RSSetViewports( 1, &viewport );
        commandList->RSSetScissorRects( 1, &scissorRect );

        ExecuteCopy();
    }

    OnImGUI( &renderContext );

    RTV = m_LinearBackbuffers[ backbufferIndex ]->GetRTV();
    commandList->OMSetRenderTargets( 1, &RTV.CPU, true, nullptr );

    DrawImGui( commandList );

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

bool SRenderer::HandleFilmResolutionChange()
{
    if ( !ResizeSceneLuminanceInputResolution( m_Scene.m_ResolutionWidth, m_Scene.m_ResolutionHeight ) )
    {
        return false;
    }

    UpdateRenderViewport( this );

    // Aspect ratio might change due to rounding error, but this is neglectable
    m_SmallResolutionWidth = std::max( 1u, (uint32_t)std::roundf( m_Scene.m_ResolutionWidth * 0.25f ) );
    m_SmallResolutionHeight = std::max( 1u, (uint32_t)std::roundf( m_Scene.m_ResolutionHeight * 0.25f ) );

    return true;
}