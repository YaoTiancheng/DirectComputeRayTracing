#pragma once

class ComputeShader;

struct ComputeJob
{
    ComputeJob();

    void Dispatch();

    void DispatchIndirect( ID3D11Buffer* buffer );

    uint32_t                                m_DispatchSizeX;
    uint32_t                                m_DispatchSizeY;
    uint32_t                                m_DispatchSizeZ;
    ComputeShader*                          m_Shader; 
    std::vector<ID3D11ShaderResourceView*>  m_SRVs;
    std::vector<ID3D11UnorderedAccessView*> m_UAVs;
    std::vector<ID3D11SamplerState*>        m_SamplerStates;
    std::vector<ID3D11Buffer*>              m_ConstantBuffers;
};