#pragma once

#include "GraphicsJob.h"

class PostProcessingRenderer
{
public:
    PostProcessingRenderer();

    bool Init( uint32_t renderWidth, uint32_t renderHeight, const GPUTexturePtr& filmTexture, const GPUBufferPtr& luminanceBuffer );

    void Execute();

    bool OnImGUI();

    void SetPostFXDisable( bool value ) { m_IsPostFXDisabled = value; m_IsJobDirty = true; }

private:
    void UpdateJob();

private:
    template <typename T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    ComPtr<ID3D11SamplerState>          m_CopySamplerState;
    ComPtr<ID3D11InputLayout>           m_ScreenQuadVertexInputLayout;

    GfxShaderPtr                        m_Shader;
    GfxShaderPtr                        m_ShaderDisablePostFX;

    GPUBufferPtr                        m_ConstantsBuffer;
    GPUBufferPtr                        m_ScreenQuadVerticesBuffer;

    GraphicsJob                         m_PostProcessingJob;

    DirectX::XMFLOAT4                   m_ConstantParams;
    bool                                m_IsConstantBufferDirty;
    bool                                m_IsJobDirty;
    bool                                m_IsPostFXDisabled;
};