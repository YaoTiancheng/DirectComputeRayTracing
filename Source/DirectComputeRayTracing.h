#pragma once

#include "Rectangle.h"
#include "Timers.h"
#include "SceneRayTrace.h"

struct SRenderContext;

struct SSceneObjectSelection
{
    void SelectPunctualLight(int index)
    {
        DeselectAll();
        m_PunctualLightSelectionIndex = index;
    }

    void SelectMesh(int index)
    {
        DeselectAll();
        m_MeshSelectionIndex = index;
    }

    void SelectMaterial(int index)
    {
        DeselectAll();
        m_MaterialSelectionIndex = index;
    }

    void SelectCamera()
    {
        DeselectAll();
        m_IsCameraSelected = true;
    }

    void SelectEnvironmentLight()
    {
        DeselectAll();
        m_IsEnvironmentLightSelected = true;
    }

    void DeselectAll()
    {
        m_PunctualLightSelectionIndex = -1;
        m_MeshSelectionIndex = -1;
        m_MaterialSelectionIndex = -1;
        m_IsCameraSelected = false;
        m_IsEnvironmentLightSelected = false;
    }

    int m_PunctualLightSelectionIndex = -1;
    int m_MeshSelectionIndex = -1;
    int m_MaterialSelectionIndex = -1;
    bool m_IsCameraSelected = false;
    bool m_IsEnvironmentLightSelected = false;
};

class CDirectComputeRayTracing
{
public:
    explicit CDirectComputeRayTracing( HWND hWnd );

    ~CDirectComputeRayTracing();

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

    void ExecuteSampleConvolution( const SRenderContext& renderContext );

    void ExecutePostProcessing( const SRenderContext& renderContext );

    void ExecuteCopy();

    bool OnPostProcessingImGui();

    void DispatchSceneLuminanceCompute( const SRenderContext& renderContext );

    HWND m_hWnd;

    std::vector<GPUTexturePtr> m_sRGBBackbuffers;

    class CScene* m_Scene = nullptr;
    SSceneObjectSelection m_ObjectSelection;

    uint32_t m_ActivePathTracerIndex = 0;

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