#pragma once

#include "Scene.h"
#include "Rectangle.h"
#include "Timers.h"
#include "BxDFTexturesBuilding.h"

class CPathTracer;
struct SRenderContext;

struct SRenderer
{
    ~SRenderer();

    bool Init();

    bool OnWndMessage( UINT message, WPARAM wParam, LPARAM lParam );

    void RenderOneFrame();

    bool LoadScene( const char* filepath, bool reset );

    bool HandleFilmResolutionChange();

    bool InitImGui( HWND hWnd );

    void ShutdownImGui();

    bool ProcessImGuiWindowMessage( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam );

    void DrawImGui( ID3D12GraphicsCommandList* commandList );

    void OnImGUI( SRenderContext* renderContext );

    bool InitSampleConvolution();

    void ExecuteSampleConvolution( const SRenderContext& renderContext );

    bool InitPostProcessing();

    void ExecutePostProcessing( const SRenderContext& renderContext );

    void ExecuteCopy();

    bool OnPostProcessingImGui();

    bool InitSceneLuminance();

    bool ResizeSceneLuminanceInputResolution( uint32_t resolutionWidth, uint32_t resolutionHeight );

    void DispatchSceneLuminanceCompute( const SRenderContext& renderContext );

    HWND m_hWnd;

    uint32_t m_FrameSeed = 0;

    bool m_IsLightGPUBufferDirty;
    bool m_IsMaterialGPUBufferDirty;
    bool m_IsFilmDirty = true;
    bool m_IsLastFrameFilmDirty = true;

    SBxDFTextures m_BxDFTextures;
    std::vector<GPUTexturePtr> m_sRGBBackbuffers;

    CScene m_Scene;
    CPathTracer* m_PathTracer[ 2 ] = { nullptr, nullptr };
    uint32_t m_ActivePathTracerIndex = 0;

    int32_t m_SampleConvolutionFilterIndex = -1;
    ComPtr<ID3D12RootSignature> m_SampleConvolutionRootSignature;
    std::shared_ptr<ID3D12PipelineState> m_SampleConvolutionPSO;

    ComPtr<ID3D12RootSignature> m_SceneLuminanceRootSignature;
    ComPtr<ID3D12PipelineState> m_SumLuminanceTo1DPSO;
    ComPtr<ID3D12PipelineState> m_SumLuminanceToSinglePSO;

    CD3D12ResourcePtr<GPUBuffer> m_SumLuminanceBuffer0;
    CD3D12ResourcePtr<GPUBuffer> m_SumLuminanceBuffer1;
    GPUBuffer* m_LuminanceResultBuffer = nullptr;

    GPUBufferPtr m_ScreenQuadVerticesBuffer;

    ComPtr<ID3D12RootSignature> m_PostProcessingRootSignature;
    ComPtr<ID3D12PipelineState> m_PostFXPSO;
    ComPtr<ID3D12PipelineState> m_PostFXAutoExposurePSO;
    ComPtr<ID3D12PipelineState> m_PostFXDisabledPSO;
    ComPtr<ID3D12PipelineState> m_CopyPSO;

    float m_LuminanceWhite = 1.f;
    float m_ManualEV100 = 15.f;
    bool m_IsPostFXEnabled = true;
    bool m_IsAutoExposureEnabled = true;
    bool m_CalculateEV100FromCamera = true;

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
    SRayTraversalCounters m_RayTraversalCounters;
    uint32_t m_RayTracingPixelPos[ 2 ] = { 0, 0 };
    float m_RayTracingSubPixelPos[ 2 ] = { 0.f, 0.f };

    uint32_t m_SPP;
    uint32_t m_CursorPixelPosOnRenderViewport[ 2 ];
    uint32_t m_CursorPixelPosOnFilm[ 2 ];
    bool m_ShowUI = true;
    bool m_ShowRayTracingUI = false;
};

class CDirectComputeRayTracing
{
public:
    explicit CDirectComputeRayTracing( HWND hWnd );

    ~CDirectComputeRayTracing();

    bool Init() { return m_Renderer->Init(); }

    bool OnWndMessage( UINT message, WPARAM wParam, LPARAM lParam ) { return m_Renderer->OnWndMessage( message, wParam, lParam ); }

    void RenderOneFrame() { m_Renderer->RenderOneFrame(); }

    SRenderer* m_Renderer = nullptr;
};