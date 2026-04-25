#include "stdafx.h"
#include "DirectComputeRayTracing.h"
#include "Scene.h"
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
#include "BxDFTexturesBuilding.h"

using namespace DirectX;

CDirectComputeRayTracing::CDirectComputeRayTracing( HWND hWnd )
{
    m_hWnd = hWnd;
    m_Scene = new CScene();
}

CDirectComputeRayTracing::~CDirectComputeRayTracing()
{
    D3D12Adapter::WaitForGPU(); // Wait for the latest frame (the last frame) to finish

    ShutdownImGui();
    
    m_Scene->m_PathTracer[ m_ActivePathTracerIndex ]->Destroy();
    for ( auto& it : m_Scene->m_PathTracer )
    {
        delete it;
    }
    delete m_Scene;

    for ( auto& backbuffer : m_sRGBBackbuffers )
    {
        backbuffer.reset();
    }

    CD3D12Resource::FlushDeleteAll();
    D3D12Adapter::Destroy();
}

bool CDirectComputeRayTracing::Init()
{
    if ( !D3D12Adapter::Init( m_hWnd ) )
        return false;

    if ( !InitImGui( m_hWnd ) )
        return false;

    CD3D12Resource::CreateDeferredDeleteQueue();

    m_Scene->m_PathTracer[ 0 ] = new CMegakernelPathTracer();
    m_Scene->m_PathTracer[ 1 ] = new CWavefrontPathTracer();

    if ( !m_Scene->m_PathTracer[ m_ActivePathTracerIndex ]->Create() )
    {
        return false;
    }

    m_Scene->m_BxDFTextures = BxDFTexturesBuilding::Build();
    if ( !m_Scene->m_BxDFTextures.AllTexturesSet() )
    {
        return false;
    }

    m_sRGBBackbuffers.resize( D3D12Adapter::GetBackbufferCount() );
    for ( uint32_t index = 0; index < D3D12Adapter::GetBackbufferCount(); ++index )
    { 
        m_sRGBBackbuffers[ index ].reset( GPUTexture::CreateFromSwapChain( DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, index ) );
        if ( !m_sRGBBackbuffers[ index ] )
        { 
            return false;
        }
    }

    if ( !m_Scene->InitSampleConvolution() )
        return false;

    if ( !m_Scene->InitPostProcessing() )
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

static void UpdateRenderViewport( CDirectComputeRayTracing* r )
{
    DXGI_SWAP_CHAIN_DESC swapChainDesc;
    D3D12Adapter::GetSwapChain()->GetDesc( &swapChainDesc );

    uint32_t renderWidth = r->m_Scene->m_ResolutionWidth;
    uint32_t renderHeight = r->m_Scene->m_ResolutionHeight;
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

static void ResizeBackbuffer( CDirectComputeRayTracing* r, uint32_t backbufferWidth, uint32_t backbufferHeight )
{
    D3D12Adapter::WaitForGPU(); // Wait for the latest frame (the last frame) to finish

    for ( GPUTexturePtr& backbuffer : r->m_sRGBBackbuffers )
    {
        backbuffer.reset();
    }

    D3D12Adapter::ResizeSwapChainBuffers( backbufferWidth, backbufferHeight );

    r->m_sRGBBackbuffers.resize( D3D12Adapter::GetBackbufferCount() );
    for ( uint32_t index = 0; index < D3D12Adapter::GetBackbufferCount(); ++index )
    {
        r->m_sRGBBackbuffers[ index ].reset( GPUTexture::CreateFromSwapChain( DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, index ) );
    }
}

bool CDirectComputeRayTracing::OnWndMessage( UINT message, WPARAM wParam, LPARAM lParam )
{
    if ( ProcessImGuiWindowMessage( m_hWnd, message, wParam, lParam ) )
        return true;

    if ( message == WM_SIZE )
    {
        if ( wParam != SIZE_MINIMIZED )
        {
            UINT width = LOWORD( lParam );
            UINT height = HIWORD( lParam );
            ResizeBackbuffer( this, width, height );
            UpdateRenderViewport( this );
        }
    }

    return m_Scene->m_Camera.OnWndMessage( message, wParam, lParam );
}

bool CDirectComputeRayTracing::LoadScene( const char* filepath, bool reset )
{
    m_Scene->m_IsFilmDirty = true; // Clear film in case scene reset failed and ray tracing being disabled.

    if ( reset )
    {
        m_Scene->Reset();
    }

    bool loadSceneResult = m_Scene->LoadFromFile( filepath );
    m_ObjectSelection.DeselectAll();
    if ( !loadSceneResult )
    {
        return false;
    }

    m_Scene->m_PathTracer[ m_ActivePathTracerIndex ]->OnSceneLoaded( m_Scene );

    if ( !HandleFilmResolutionChange() )
    {
        return false;
    }

    m_NewResolutionWidth = m_Scene->m_ResolutionWidth;
    m_NewResolutionHeight = m_Scene->m_ResolutionHeight;

    m_Scene->m_IsMaterialGPUBufferDirty = true;
    m_Scene->m_IsLightGPUBufferDirty = true;
    m_Scene->m_IsInstanceFlagsBufferDirty = true;

    m_RayTracingHasHit = false;

    return true;
}

static void ClearFilmTexture( CScene* scene )
{
    ID3D12GraphicsCommandList* commandList = D3D12Adapter::GetCommandList();
    const float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    commandList->ClearRenderTargetView( scene->m_FilmTexture->GetRTV().CPU, clearColor, 0, nullptr );
}

static void DispatchRayTracing( CDirectComputeRayTracing* r, CScene* scene, SRenderContext* renderContext )
{
    scene->m_IsFilmDirty = scene->m_IsFilmDirty || scene->m_IsLightGPUBufferDirty || scene->m_IsMaterialGPUBufferDirty || scene->m_IsInstanceFlagsBufferDirty
        || scene->m_Camera.IsDirty() || scene->m_PathTracer[ r->m_ActivePathTracerIndex ]->AcquireFilmClearTrigger();

    const bool isResolutionChanged = ( scene->m_IsFilmDirty != scene->m_IsLastFrameFilmDirty );
    renderContext->m_IsSmallResolutionEnabled = scene->m_IsFilmDirty;

    scene->m_IsLastFrameFilmDirty = scene->m_IsFilmDirty;

    renderContext->m_CurrentResolutionWidth = renderContext->m_IsSmallResolutionEnabled ? r->m_SmallResolutionWidth : scene->m_ResolutionWidth;
    renderContext->m_CurrentResolutionRatio = (float)renderContext->m_CurrentResolutionWidth / scene->m_ResolutionWidth;
    renderContext->m_CurrentResolutionHeight = renderContext->m_IsSmallResolutionEnabled ? r->m_SmallResolutionHeight : scene->m_ResolutionHeight;

    if ( scene->m_IsFilmDirty || isResolutionChanged )
    {
        // Transition the film texture to RTV
        if ( scene->m_FilmTextureStates != D3D12_RESOURCE_STATE_RENDER_TARGET )
        {
            D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition( scene->m_FilmTexture->GetTexture(),
                scene->m_FilmTextureStates, D3D12_RESOURCE_STATE_RENDER_TARGET );
            D3D12Adapter::GetCommandList()->ResourceBarrier( 1, &barrier );

            scene->m_FilmTextureStates = D3D12_RESOURCE_STATE_RENDER_TARGET;
        }

        ClearFilmTexture( scene );

        if ( r->m_FrameSeedType == CDirectComputeRayTracing::EFrameSeedType::SampleCount )
        {
            scene->m_FrameSeed = 0;
        }

        r->m_SPP = 0;

        scene->m_PathTracer[ r->m_ActivePathTracerIndex ]->ResetImage();
    }

    if ( scene->m_IsLightGPUBufferDirty )
    {
        scene->UpdateLightGPUData();
    }

    if ( scene->m_IsMaterialGPUBufferDirty )
    {
        scene->UpdateMaterialGPUData();
    }

    if ( scene->m_IsInstanceFlagsBufferDirty )
    {
        scene->UpdateInstanceFlagsGPUData();
    }

    scene->m_PathTracer[ r->m_ActivePathTracerIndex ]->Render( scene, *renderContext );

    if ( scene->m_PathTracer[ r->m_ActivePathTracerIndex ]->IsImageComplete() )
    {
        if ( r->m_FrameSeedType != CDirectComputeRayTracing::EFrameSeedType::Fixed )
        {
            scene->m_FrameSeed++;
        }

        ++r->m_SPP;
    }
    
    scene->m_Camera.ClearDirty();
    scene->m_IsLightGPUBufferDirty = false;
    scene->m_IsMaterialGPUBufferDirty = false;
    scene->m_IsInstanceFlagsBufferDirty = false;
    scene->m_IsFilmDirty = false;
}

void CDirectComputeRayTracing::RenderOneFrame()
{
    m_FrameTimer.BeginFrame();

    SRenderContext renderContext;

    D3D12Adapter::BeginCurrentFrame();

    D3D12_VIEWPORT viewport;
    D3D12_RECT scissorRect;
    ID3D12GraphicsCommandList* commandList = D3D12Adapter::GetCommandList();
    SD3D12DescriptorHandle RTV;

    if ( m_Scene->m_HasValidScene )
    { 
        m_Scene->m_Camera.Update( m_FrameTimer.GetCurrentFrameDeltaTime() );
        m_Scene->RebuildMeshFlagsIfDirty();

        m_Scene->AllocateAndUpdateTextureDescriptorTable();

        DispatchRayTracing( this, m_Scene, &renderContext );

        if ( m_Scene->m_PathTracer[ m_ActivePathTracerIndex ]->IsImageComplete() || renderContext.m_IsSmallResolutionEnabled )
        {
            ExecuteSampleConvolution( renderContext );

            DispatchSceneLuminanceCompute( renderContext );

            RTV = m_Scene->m_RenderResultTexture->GetRTV();
            commandList->OMSetRenderTargets( 1, &RTV.CPU, true, nullptr );

            viewport = { 0.0f, 0.0f, (float)m_Scene->m_ResolutionWidth, (float)m_Scene->m_ResolutionHeight, 0.0f, 1.0f };
            scissorRect = CD3DX12_RECT( 0, 0, m_Scene->m_ResolutionWidth, m_Scene->m_ResolutionHeight );
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

    if ( m_Scene->m_HasValidScene )
    {
        viewport = { (float)m_RenderViewport.m_TopLeftX, (float)m_RenderViewport.m_TopLeftY, (float)m_RenderViewport.m_Width, (float)m_RenderViewport.m_Height, 0.0f, 1.0f };
        scissorRect = CD3DX12_RECT( m_RenderViewport.m_TopLeftX, m_RenderViewport.m_TopLeftY,
            m_RenderViewport.m_TopLeftX + m_RenderViewport.m_Width, m_RenderViewport.m_TopLeftY + m_RenderViewport.m_Height );
        commandList->RSSetViewports( 1, &viewport );
        commandList->RSSetScissorRects( 1, &scissorRect );

        ExecuteCopy();
    }

    OnImGUI( &renderContext );
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
    m_Scene->m_IsLightBufferRead = true;
    m_Scene->m_IsMaterialBufferRead = true;
    m_Scene->m_IsInstanceFlagsBufferRead = true;
}

bool CDirectComputeRayTracing::HandleFilmResolutionChange()
{
    if ( !m_Scene->ResizeSceneLuminanceInputResolution( m_Scene->m_ResolutionWidth, m_Scene->m_ResolutionHeight ) )
    {
        return false;
    }

    UpdateRenderViewport( this );

    // Aspect ratio might change due to rounding error, but this is neglectable
    m_SmallResolutionWidth = std::max( 1u, (uint32_t)std::roundf( m_Scene->m_ResolutionWidth * 0.25f ) );
    m_SmallResolutionHeight = std::max( 1u, (uint32_t)std::roundf( m_Scene->m_ResolutionHeight * 0.25f ) );

    return true;
}