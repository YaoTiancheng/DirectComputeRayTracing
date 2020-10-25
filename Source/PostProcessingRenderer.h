#pragma once

#include "Shader.h"
#include "GPUBuffer.h"
#include "GraphicsJob.h"

class GPUTexture;

class PostProcessingRenderer
{
public:
    bool Init( uint32_t resolutionWidth, uint32_t resolutionHeight, const std::unique_ptr<GPUTexture>& filmTexture, const std::unique_ptr<GPUBuffer>& luminanceBuffer );

    void Execute( const std::unique_ptr<GPUTexture>& renderTargetTexture );

private:
    D3D11_VIEWPORT                      m_DefaultViewport;

    template <typename T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    ComPtr<ID3D11SamplerState>          m_CopySamplerState;
    ComPtr<ID3D11InputLayout>           m_ScreenQuadVertexInputLayout;

    using GfxShaderPtr = std::unique_ptr<GfxShader>;
    GfxShaderPtr                        m_Shader;

    using GPUBufferPtr = std::unique_ptr<GPUBuffer>;
    GPUBufferPtr                        m_ConstantsBuffer;
    GPUBufferPtr                        m_ScreenQuadVerticesBuffer;

    GraphicsJob                         m_PostProcessingJob;
};