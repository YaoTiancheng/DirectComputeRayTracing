#pragma once

#include "Camera.h"
#include "ComputeJob.h"
#include "PostProcessingRenderer.h"
#include "SceneLuminanceRenderer.h"
#include "Timers.h"
#include "Rectangle.h"
#include "../Shaders/PointLight.inc.hlsl"
#include "../Shaders/Material.inc.hlsl"


struct RayTracingConstants
{
    DirectX::XMFLOAT4X4 cameraTransform;
    DirectX::XMFLOAT2   filmSize;
    uint32_t            resolutionX;
    uint32_t            resolutionY;
    DirectX::XMFLOAT4   background;
    uint32_t            maxBounceCount;
    uint32_t            primitiveCount;
    uint32_t            pointLightCount;
    float               filmDistance;
};


class CDirectComputeRayTracing
{
public:
    explicit CDirectComputeRayTracing( HWND hWnd );

    ~CDirectComputeRayTracing();

    bool Init();

    bool ResetScene();

    void AddOneSampleAndRender();

    bool OnWndMessage( UINT message, WPARAM wParam, LPARAM lParam );

    void OnImGUI();

private:
    void AddOneSample();

    bool UpdateResources();

    void DispatchRayTracing();

    void ClearFilmTexture();

    void UpdateRayTracingJob();

    void UpdateRenderViewport( uint32_t backbufferWidth, uint32_t backbufferHeight );

    void CreateEnvironmentTextureFromCurrentFilepath();

private:
    

    static const int kMaxPointLightsCount = 64;
    static const int kRayTracingKernelCount = 6;

    HWND                                m_hWnd;

    Camera                              m_Camera;
    std::vector<PointLight>             m_PointLights;
    std::vector<Material>               m_Materials;
    std::vector<std::string>            m_MaterialNames;

    RayTracingConstants                 m_RayTracingConstants;

    uint32_t                            m_FrameSeed;

    bool                                m_IsFilmDirty;
    bool                                m_IsConstantBufferDirty;
    bool                                m_IsPointLightBufferDirty;
    bool                                m_IsMaterialBufferDirty;
    bool                                m_IsRayTracingJobDirty;

    template <typename T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;
    ComPtr<ID3D11SamplerState>          m_UVClampSamplerState;

    ComputeShaderPtr                    m_RayTracingShader[ kRayTracingKernelCount ];

    GPUTexturePtr                       m_FilmTexture;
    GPUTexturePtr                       m_RenderResultTexture;
    GPUTexturePtr                       m_sRGBBackbuffer;
    GPUTexturePtr                       m_LinearBackbuffer;
    GPUTexturePtr                       m_CookTorranceCompETexture;
    GPUTexturePtr                       m_CookTorranceCompEAvgTexture;
    GPUTexturePtr                       m_CookTorranceCompInvCDFTexture;
    GPUTexturePtr                       m_CookTorranceCompPdfScaleTexture;
    GPUTexturePtr                       m_CookTorranceCompEFresnelTexture;
    GPUTexturePtr                       m_EnvironmentTexture;

    GPUBufferPtr                        m_RayTracingConstantsBuffer;
    GPUBufferPtr                        m_RayTracingFrameConstantBuffer;
    GPUBufferPtr                        m_VerticesBuffer;
    GPUBufferPtr                        m_TrianglesBuffer;
    GPUBufferPtr                        m_BVHNodesBuffer;
    GPUBufferPtr                        m_PointLightsBuffer;
    GPUBufferPtr                        m_MaterialIdsBuffer;
    GPUBufferPtr                        m_MaterialsBuffer;

    ComputeJob                          m_RayTracingJob;

    PostProcessingRenderer              m_PostProcessing;
    SceneLuminanceRenderer              m_SceneLuminance;

    int                                 m_PointLightSelectionIndex;
    int                                 m_MaterialSelectionIndex;
    int                                 m_RayTracingKernelIndex;

    enum class EFrameSeedType { FrameIndex = 0, SampleCount = 1, Fixed = 2, _Count = 3 };
    EFrameSeedType                      m_FrameSeedType;

    FrameTimer                          m_FrameTimer;

    SRectangle                          m_RenderViewport;

    std::string                         m_EnvironmentImageFilepath;
};