#pragma once

#include "PathTracer.h"
#include "D3D12Resource.h"

class CScene;
struct SBxDFTextures;

class CWavefrontPathTracer : public CPathTracer
{
public:
    CWavefrontPathTracer();

    virtual ~CWavefrontPathTracer()
    {
    }

    virtual bool Create() override;

    virtual void Destroy() override;

    virtual void OnSceneLoaded( SRenderer* renderer ) override;

    virtual void Render( SRenderer* renderer, const SRenderContext& renderContext ) override;

    virtual void ResetImage() override;

    virtual bool IsImageComplete() override;

    virtual void OnImGUI( SRenderer* renderer );

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

    bool CompileAndCreateShader( CScene* scene, EShaderKernel kernel );

    void GetBlockDimension( uint32_t* width, uint32_t* height );

    void RenderOneIteration( CScene* scene, const SBxDFTextures& BxDFTextures, bool isInitialIteration );

    CD3D12ComPtr<ID3D12RootSignature> m_RootSignature;
    CD3D12ComPtr<ID3D12PipelineState> m_PSOs[ (int)EShaderKernel::_Count ];

    CD3D12ResourcePtr<GPUBuffer> m_RayBuffer;
    CD3D12ResourcePtr<GPUBuffer> m_RayHitBuffer;
    CD3D12ResourcePtr<GPUBuffer> m_ShadowRayBuffer;
    CD3D12ResourcePtr<GPUBuffer> m_PixelPositionBuffer;
    CD3D12ResourcePtr<GPUBuffer> m_PixelSampleBuffer;
    CD3D12ResourcePtr<GPUBuffer> m_RngBuffer;
    CD3D12ResourcePtr<GPUBuffer> m_LightSamplingResultsBuffer;
    CD3D12ResourcePtr<GPUBuffer> m_PathAccumulationBuffer;
    CD3D12ResourcePtr<GPUBuffer> m_FlagsBuffer;
    CD3D12ResourcePtr<GPUBuffer> m_NextBlockIndexBuffer;
    CD3D12ResourcePtr<GPUBuffer> m_IndirectArgumentBuffer[ 4 ];
    CD3D12ResourcePtr<GPUBuffer> m_QueueBuffers[ 4 ];
    CD3D12ResourcePtr<GPUBuffer> m_QueueCounterBuffers[ 2 ];
    CD3D12ResourcePtr<GPUBuffer> m_ControlConstantBuffer;
    CD3D12ResourcePtr<GPUBuffer> m_NewPathConstantBuffer;
    CD3D12ResourcePtr<GPUBuffer> m_MaterialConstantBuffer;

    static const uint32_t s_QueueCounterStagingBufferCount = 3;
    CD3D12ResourcePtr<GPUBuffer> m_QueueCounterStagingBuffer[ s_QueueCounterStagingBufferCount ];
    uint32_t m_QueueCounterStagingBufferIndex = 0;
    uint32_t m_StagingBufferReadyCountdown = s_QueueCounterStagingBufferCount;

    bool m_NewImage = true;
    bool m_FilmClearTrigger = false;

    uint32_t m_IterationPerFrame = 2;
    uint32_t m_BlockDimensionIndex = 0;
    uint32_t m_BarrierMode;
};