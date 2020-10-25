#pragma once

#include "Camera.h"
#include "ComputeJob.h"
#include "PostProcessingRenderer.h"


struct RayTracingConstants
{
    uint32_t            maxBounceCount;
    uint32_t            primitiveCount;
    uint32_t            pointLightCount;
    uint32_t            samplesCount;
    uint32_t            resolutionX;
    uint32_t            resolutionY;
    DirectX::XMFLOAT2   filmSize;
    float               filmDistance;
    uint32_t            padding[ 3 ];
    DirectX::XMFLOAT4X4 cameraTransform;
    DirectX::XMFLOAT4   background;
};


class Scene
{
public:
    Scene();

    ~Scene();

    bool Init();

    bool ResetScene();

    void AddOneSampleAndRender();

    bool OnWndMessage( UINT message, WPARAM wParam, LPARAM lParam );

private:
    void AddOneSample();

    bool UpdateResources();

    void DispatchSumLuminance();

    void DispatchRayTracing();

    void ClearFilmTexture();

    void UpdateRayTracingJob();

    void UpdateSumLuminanceJobs();

private:
    static const int kMaxSamplesCount = 65536;


    Camera                              m_Camera;

    RayTracingConstants                 m_RayTracingConstants;
    uint32_t                            m_SumLuminanceBlockCountX;
    uint32_t                            m_SumLuminanceBlockCountY;

    bool                                m_IsFilmDirty;

    std::mt19937                        m_MersenneURBG;
    std::uniform_real_distribution<float>   m_UniformRealDistribution;

    template <typename T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;
    ComPtr<ID3D11SamplerState>          m_UVClampSamplerState;

    ComputeShaderPtr                    m_RayTracingShader;
    ComputeShaderPtr                    m_SumLuminanceTo1DShader;
    ComputeShaderPtr                    m_SumLuminanceToSingleShader;

    GPUTexturePtr                       m_FilmTexture;
    GPUTexturePtr                       m_DefaultRenderTarget;
    GPUTexturePtr                       m_CookTorranceCompETexture;
    GPUTexturePtr                       m_CookTorranceCompEAvgTexture;
    GPUTexturePtr                       m_CookTorranceCompInvCDFTexture;
    GPUTexturePtr                       m_CookTorranceCompPdfScaleTexture;
    GPUTexturePtr                       m_CookTorranceCompEFresnelTexture;
    GPUTexturePtr                       m_EnvironmentTexture;

    GPUBufferPtr                        m_RayTracingConstantsBuffer;
    GPUBufferPtr                        m_SamplesBuffer;
    GPUBufferPtr                        m_VerticesBuffer;
    GPUBufferPtr                        m_TrianglesBuffer;
    GPUBufferPtr                        m_BVHNodesBuffer;
    GPUBufferPtr                        m_PointLightsBuffer;
    GPUBufferPtr                        m_MaterialIdsBuffer;
    GPUBufferPtr                        m_MaterialsBuffer;
    GPUBufferPtr                        m_SampleCounterBuffer;
    GPUBufferPtr                        m_SumLuminanceBuffer0;
    GPUBufferPtr                        m_SumLuminanceBuffer1;
    GPUBufferPtr                        m_SumLuminanceConstantsBuffer0;
    GPUBufferPtr                        m_SumLuminanceConstantsBuffer1;

    ComputeJob                          m_RayTracingJob;
    ComputeJob                          m_SumLuminanceTo1DJob;
    ComputeJob                          m_SumLuminanceToSingleJob;

    PostProcessingRenderer              m_PostProcessing;
};