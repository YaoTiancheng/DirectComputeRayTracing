#pragma once

#include "PathTracer.h"

class CScene;

class CWavefrontPathTracer : public CPathTracer
{
public:
    explicit CWavefrontPathTracer( CScene* scene )
        : m_Scene( scene )
    {
    }

    virtual ~CWavefrontPathTracer()
    {
    }

    virtual bool Create() override;

    virtual void Destroy() override;

    virtual void OnSceneLoaded() override;

    virtual void Render( const SRenderContext& renderContext, const SBxDFTextures& BxDFTextures ) override;

    virtual void ResetImage() override;

    virtual bool IsImageComplete() override;

    virtual void OnImGUI();

    virtual bool AcquireFilmClearTrigger();

private:

    enum class EShaderKernel
    {
          ExtensionRayCast
        , ShadowRayCast
        , NewPath
        , Material
        , Control
        , FillIndirectArguments
        , SetIdle
        , _Count
    };

    bool CompileAndCreateShader( EShaderKernel kernel );

    void GetBlockDimension( uint32_t* width, uint32_t* height );

    void RenderOneIteration( const SRenderContext& renderContext, const SBxDFTextures& BxDFTextures, bool isInitialIteration );

    CScene* m_Scene;

    ComPtr<ID3D12RootSignature> m_RootSignature;
    std::shared_ptr<ID3D12PipelineState> m_PSOs[ (int)EShaderKernel::_Count ];

    GPUBufferPtr m_RayBuffer;
    GPUBufferPtr m_RayHitBuffer;
    GPUBufferPtr m_ShadowRayBuffer;
    GPUBufferPtr m_PixelPositionBuffer;
    GPUBufferPtr m_PixelSampleBuffer;
    GPUBufferPtr m_RngBuffer;
    GPUBufferPtr m_LightSamplingResultsBuffer;
    GPUBufferPtr m_PathAccumulationBuffer;
    GPUBufferPtr m_FlagsBuffer;
    GPUBufferPtr m_NextBlockIndexBuffer;
    GPUBufferPtr m_IndirectArgumentBuffer[ 4 ];
    GPUBufferPtr m_QueueBuffers[ 4 ];
    GPUBufferPtr m_QueueCounterBuffers[ 2 ];
    GPUBufferPtr m_QueueConstantsBuffers[ 2 ];
    GPUBufferPtr m_ControlConstantBuffer;
    GPUBufferPtr m_NewPathConstantBuffer;
    GPUBufferPtr m_MaterialConstantBuffer;

    static const uint32_t s_QueueCounterStagingBufferCount = 2;
    GPUBufferPtr m_QueueCounterStagingBuffer[ s_QueueCounterStagingBufferCount ];
    uint32_t m_QueueCounterStagingBufferIndex = 0;

    bool m_NewImage = true;
    bool m_FilmClearTrigger = false;

    uint32_t m_IterationPerFrame = 2;
    uint32_t m_BlockDimensionIndex = 0;
};