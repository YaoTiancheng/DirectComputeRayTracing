#pragma once

#include "ComputeJob.h"

class SceneLuminanceRenderer
{
public:
    bool Init( uint32_t resolutionWidth, uint32_t resolutionHeight, const GPUTexturePtr& filmTexture );

    void Dispatch( uint32_t resolutionWidth, uint32_t resolutionHeight );

    const GPUBufferPtr& GetLuminanceResultBuffer() const { return m_SumLuminanceBuffer1; }

    void OnImGUI();

private:
    ComputeShaderPtr    m_SumLuminanceTo1DShader;
    ComputeShaderPtr    m_SumLuminanceToSingleShader;
    GPUBufferPtr        m_SumLuminanceBuffer0;
    GPUBufferPtr        m_SumLuminanceBuffer1;
    GPUBufferPtr        m_SumLuminanceConstantsBuffer0;
    GPUBufferPtr        m_SumLuminanceConstantsBuffer1;

    ComputeJob          m_SumLuminanceTo1DJob;
    ComputeJob          m_SumLuminanceToSingleJob;
};