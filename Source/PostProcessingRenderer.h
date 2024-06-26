#pragma once

#include "GraphicsJob.h"
#include "SceneLuminanceRenderer.h"

struct SRenderContext;
class CScene;

class PostProcessingRenderer
{
public:
    PostProcessingRenderer();

    bool Init();

    bool SetTextures( uint32_t renderWidth, uint32_t renderHeight, const GPUTexturePtr& filmTexture, const GPUTexturePtr& renderResultTexture );

    void ExecuteLuminanceCompute( const SRenderContext& renderContext );

    void ExecutePostFX( const SRenderContext& renderContext, const CScene& scene );

    void ExecuteCopy();

    bool OnImGUI();

private:
    void UpdateJob( bool enablePostFX );

private:
    SceneLuminanceRenderer m_LuminanceRenderer;

    template <typename T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    ComPtr<ID3D11SamplerState>          m_LinearSamplerState;
    ComPtr<ID3D11SamplerState>          m_PointSamplerState;
    ComPtr<ID3D11InputLayout>           m_ScreenQuadVertexInputLayout;

    GfxShaderPtr                        m_PostFXShader;
    GfxShaderPtr                        m_PostFXAutoExposureShader;
    GfxShaderPtr                        m_PostFXDisabledShader;
    GfxShaderPtr                        m_CopyShader;

    GPUBufferPtr                        m_ConstantsBuffer;
    GPUBufferPtr                        m_ScreenQuadVerticesBuffer;

    GraphicsJob                         m_PostFXJob;
    GraphicsJob                         m_CopyJob;

    float                               m_LuminanceWhite;
    float                               m_ManualEV100;
    bool                                m_IsPostFXEnabled;
    bool                                m_IsAutoExposureEnabled;
    bool                                m_CalculateEV100FromCamera;
};