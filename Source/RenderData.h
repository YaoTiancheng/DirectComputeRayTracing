#pragma once

struct SRenderData
{
    GPUTexturePtr                       m_FilmTexture;
    GPUTexturePtr                       m_SamplePositionTexture;
    GPUTexturePtr                       m_SampleValueTexture;
    GPUTexturePtr                       m_RenderResultTexture;
    GPUTexturePtr                       m_sRGBBackbuffer;
    GPUTexturePtr                       m_LinearBackbuffer;
    GPUTexturePtr                       m_BRDFTexture;
    GPUTexturePtr                       m_BRDFAvgTexture;
    GPUTexturePtr                       m_BRDFDielectricTexture;
    GPUTexturePtr                       m_BSDFTexture;
    GPUTexturePtr                       m_BSDFAvgTexture;
    GPUBufferPtr                        m_RayTracingFrameConstantBuffer;

    template <typename T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;
    ComPtr<ID3D11SamplerState>          m_UVClampSamplerState;
};