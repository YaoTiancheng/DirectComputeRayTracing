#pragma once

#include "ComputeJob.h"
#include "Shader.h"

class SceneLuminanceRenderer
{
public:
    bool Init();

    bool SetFilmTexture( uint32_t resolutionWidth, uint32_t resolutionHeight, const GPUTexturePtr& filmTexture );

    void Dispatch( uint32_t resolutionWidth, uint32_t resolutionHeight );

    const GPUBuffer* GetLuminanceResultBuffer() const { return m_LuminanceResultBuffer; }

    void OnImGUI();

private:
    ComputeShaderPtr    m_SumLuminanceTo1DShader;
    ComputeShaderPtr    m_SumLuminanceToSingleShader;
    GPUBufferPtr        m_SumLuminanceBuffer0;
    GPUBufferPtr        m_SumLuminanceBuffer1;
    GPUBufferPtr        m_SumLuminanceConstantsBuffer0;
    GPUBufferPtr        m_SumLuminanceConstantsBuffer1;
    GPUBuffer*          m_LuminanceResultBuffer = nullptr;

    ComputeJob          m_SumLuminanceTo1DJob;
    ComputeJob          m_SumLuminanceToSingleJob;
};