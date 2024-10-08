#pragma once

class CScene;

class SceneLuminanceRenderer
{
public:
    bool Init();

    bool SetFilmTexture( uint32_t resolutionWidth, uint32_t resolutionHeight, const GPUTexturePtr& filmTexture );

    void Dispatch( const CScene& scene, uint32_t resolutionWidth, uint32_t resolutionHeight );

    const GPUBuffer* GetLuminanceResultBuffer() const { return m_LuminanceResultBuffer; }

    void OnImGUI();

private:
    ComPtr<ID3D12RootSignature> m_RootSignature;
    ComPtr<ID3D12PipelineState> m_SumLuminanceTo1DPSO;
    ComPtr<ID3D12PipelineState> m_SumLuminanceToSinglePSO;

    GPUBufferPtr m_SumLuminanceBuffer0;
    GPUBufferPtr m_SumLuminanceBuffer1;
    GPUBuffer* m_LuminanceResultBuffer = nullptr;
};