#pragma once

#include "SceneLuminanceRenderer.h"

struct SRenderContext;
class CScene;

class PostProcessingRenderer
{
public:
    PostProcessingRenderer();

    bool Init();

    bool SetTextures( uint32_t renderWidth, uint32_t renderHeight, const GPUTexturePtr& filmTexture, const GPUTexturePtr& renderResultTexture );

    void ExecuteLuminanceCompute( const CScene& scene, const SRenderContext& renderContext );

    void ExecutePostFX( const SRenderContext& renderContext, const CScene& scene );

    void ExecuteCopy( const CScene& scene );

    bool OnImGUI();

private:
    SceneLuminanceRenderer m_LuminanceRenderer;

    GPUBufferPtr m_ScreenQuadVerticesBuffer;

    ComPtr<ID3D12RootSignature> m_RootSignature;
    ComPtr<ID3D12PipelineState> m_PostFXPSO;
    ComPtr<ID3D12PipelineState> m_PostFXAutoExposurePSO;
    ComPtr<ID3D12PipelineState> m_PostFXDisabledPSO;
    ComPtr<ID3D12PipelineState> m_CopyPSO;

    float m_LuminanceWhite;
    float m_ManualEV100;
    bool m_IsPostFXEnabled;
    bool m_IsAutoExposureEnabled;
    bool m_CalculateEV100FromCamera;
};