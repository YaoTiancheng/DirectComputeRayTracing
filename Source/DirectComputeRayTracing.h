#pragma once

#include "Scene.h"
#include "Rectangle.h"
#include "Timers.h"
#include "BxDFTexturesBuilding.h"
#include "PostProcessingRenderer.h"

class CPathTracer;
struct SRenderContext;

class CDirectComputeRayTracing
{
public:
    explicit CDirectComputeRayTracing( HWND hWnd );

    ~CDirectComputeRayTracing();

    bool Init() { return m_Renderer->Init(); }

    bool OnWndMessage( UINT message, WPARAM wParam, LPARAM lParam ) { return m_Renderer->OnWndMessage( message, wParam, lParam ); }

    void RenderOneFrame() { m_Renderer->RenderOneFrame(); }

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

        struct MainLoopPrivate;

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
        PostProcessingRenderer m_PostProcessing;

        int32_t m_SampleConvolutionFilterIndex = -1;
        ComPtr<ID3D12RootSignature> m_SampleConvolutionRootSignature;
        std::shared_ptr<ID3D12PipelineState> m_SampleConvolutionPSO;

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

    SRenderer* m_Renderer = nullptr;
};