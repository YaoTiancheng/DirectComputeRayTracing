#pragma once

#include "GraphicsJob.h"

class PostProcessingRenderer
{
public:
    PostProcessingRenderer();

    bool Init( uint32_t resolutionWidth, uint32_t resolutionHeight, const GPUTexturePtr& filmTexture, const GPUBufferPtr& luminanceBuffer );

    void Execute( const GPUTexturePtr& renderTargetTexture );

    bool OnImGUI();

    void SetPostFXDisable( bool value ) { m_IsPostFXDisabled = value; m_IsJobDirty = true; }

private:
    void UpdateJob();

private:
    D3D11_VIEWPORT                      m_DefaultViewport;

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