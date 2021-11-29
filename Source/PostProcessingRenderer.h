#pragma once

#include "GraphicsJob.h"

class PostProcessingRenderer
{
public:
    PostProcessingRenderer();

    bool Init( uint32_t renderWidth, uint32_t renderHeight, const GPUTexturePtr& filmTexture, const GPUTexturePtr& renderResultTexture, const GPUBufferPtr& luminanceBuffer );

    void ExecutePostFX( uint32_t resolutionWidth, uint32_t resolutionHeight, float rayTracingViewportRatio );

    void ExecuteCopy();

    bool OnImGUI();

    void SetPostFXDisable( bool value ) { m_IsPostFXDisabled = value; m_IsJobDirty = true; }

private:
    void UpdateJob();

private:
    template <typename T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    ComPtr<ID3D11SamplerState>          m_LinearSamplerState;
    ComPtr<ID3D11SamplerState>          m_PointSamplerState;
    ComPtr<ID3D11InputLayout>           m_ScreenQuadVertexInputLayout;

    GfxShaderPtr                        m_PostFXShader;
    GfxShaderPtr                        m_PostFXDisabledShader;
    GfxShaderPtr                        m_CopyShader;

    GPUBufferPtr                        m_ConstantsBuffer;
    GPUBufferPtr                        m_ScreenQuadVerticesBuffer;

    GraphicsJob                         m_PostFXJob;
    GraphicsJob                         m_CopyJob;

    DirectX::XMFLOAT4                   m_ConstantParams;
    bool                                m_IsJobDirty;
    bool                                m_IsPostFXDisabled;
};