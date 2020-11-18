#pragma once

#include "GraphicsJob.h"

class PostProcessingRenderer
{
public:
    PostProcessingRenderer();

    bool Init( uint32_t resolutionWidth, uint32_t resolutionHeight, const GPUTexturePtr& filmTexture, const GPUBufferPtr& luminanceBuffer );

    void Execute( const GPUTexturePtr& renderTargetTexture );

    bool OnImGUI();

private:
    D3D11_VIEWPORT                      m_DefaultViewport;

    template <typename T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    ComPtr<ID3D11SamplerState>          m_CopySamplerState;
    ComPtr<ID3D11InputLayout>           m_ScreenQuadVertexInputLayout;

    GfxShaderPtr                        m_Shader;

    GPUBufferPtr                        m_ConstantsBuffer;
    GPUBufferPtr                        m_ScreenQuadVerticesBuffer;

    GraphicsJob                         m_PostProcessingJob;

    DirectX::XMFLOAT4                   m_ConstantParams;
    bool                                m_IsConstantBufferDirty;
};