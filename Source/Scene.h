#pragma once

#include "Primitive.h"
#include "Camera.h"
#include "BVHAccel.h"
#include "GPUBuffer.h"
#include "GPUTexture.h"
#include "Shader.h"

struct PointLight
{
    DirectX::XMFLOAT3   position;
    DirectX::XMFLOAT3   color;
};

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

    bool Init();

    bool ResetScene();

    void AddOneSampleAndRender();

    bool OnWndMessage( UINT message, WPARAM wParam, LPARAM lParam );

private:
    void AddOneSample();

    void DoPostProcessing();

    bool UpdateResources();

    void DispatchRayTracing();

    void ClearFilmTexture();


private:
    static const int kMaxSamplesCount = 65536;


    Camera                              m_Camera;

    D3D11_VIEWPORT                      m_DefaultViewport;

    RayTracingConstants                 m_RayTracingConstants;

    bool                                m_IsFilmDirty;

    std::mt19937                        m_MersenneURBG;
    std::uniform_real_distribution<float>   m_UniformRealDistribution;

    template <typename T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;
    ComPtr<ID3D11SamplerState>          m_CopySamplerState;
    ComPtr<ID3D11SamplerState>          m_UVClampSamplerState;
    ComPtr<ID3D11InputLayout>           m_ScreenQuadVertexInputLayout;

    using GfxShaderPtr = std::unique_ptr<GfxShader>;
    GfxShaderPtr                        m_PostFXShader;
    using ComputeShaderPtr = std::unique_ptr<ComputeShader>;
    ComputeShaderPtr                    m_RayTracingShader;

    using GPUTexturePtr = std::unique_ptr<GPUTexture>;
    GPUTexturePtr                       m_FilmTexture;
    GPUTexturePtr                       m_DefaultRenderTarget;
    GPUTexturePtr                       m_CookTorranceCompETexture;
    GPUTexturePtr                       m_CookTorranceCompEAvgTexture;
    GPUTexturePtr                       m_CookTorranceCompInvCDFTexture;
    GPUTexturePtr                       m_CookTorranceCompPdfScaleTexture;
    GPUTexturePtr                       m_CookTorranceCompEFresnelTexture;

    using GPUBufferPtr = std::unique_ptr<GPUBuffer>;
    GPUBufferPtr                        m_RayTracingConstantsBuffer;
    GPUBufferPtr                        m_CookTorranceCompTextureConstantsBuffer;
    GPUBufferPtr                        m_SamplesBuffer;
    GPUBufferPtr                        m_VerticesBuffer;
    GPUBufferPtr                        m_TrianglesBuffer;
    GPUBufferPtr                        m_BVHNodesBuffer;
    GPUBufferPtr                        m_PointLightsBuffer;
    GPUBufferPtr                        m_MaterialIdsBuffer;
    GPUBufferPtr                        m_MaterialsBuffer;
    GPUBufferPtr                        m_ScreenQuadVerticesBuffer;
};