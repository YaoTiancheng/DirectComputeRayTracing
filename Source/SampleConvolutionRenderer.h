#pragma once

class CScene;
struct SRenderContext;

class CSampleConvolutionRenderer
{
public:
    bool Init();

    void Execute( const SRenderContext& renderContext, const CScene& scene );

private:
    void CompileShader( int32_t filterIndex );

    int32_t m_FilterIndex = -1;
    ComPtr<ID3D12RootSignature> m_RootSignature;
    std::shared_ptr<ID3D12PipelineState> m_PSO;
};