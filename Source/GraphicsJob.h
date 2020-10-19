#pragma once

class GfxShader;
class GPUBuffer;

struct GraphicsJob
{
    GraphicsJob();

    void Dispatch();
    
    GPUBuffer*                              m_VertexBuffer;
    ID3D11InputLayout*                      m_InputLayout;
    uint32_t                                m_VertexCount;
    uint32_t                                m_VertexStride;
    GfxShader*                              m_Shader;
    std::vector<ID3D11ShaderResourceView*>  m_SRVs;
    std::vector<ID3D11SamplerState*>        m_SamplerStates;
    std::vector<ID3D11Buffer*>              m_ConstantBuffers;
};