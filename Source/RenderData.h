#pragma once

struct SRenderData
{
    GPUTexturePtr                       m_FilmTexture;
    GPUTexturePtr                       m_SamplePositionTexture;
    GPUTexturePtr                       m_SampleValueTexture;
    GPUTexturePtr                       m_RenderResultTexture;
    GPUTexturePtr                       m_sRGBBackbuffer;
    GPUTexturePtr                       m_LinearBackbuffer;
    GPUTexturePtr                       m_CookTorranceCompETexture;
    GPUTexturePtr                       m_CookTorranceCompEAvgTexture;
    GPUTexturePtr                       m_CookTorranceCompInvCDFTexture;
    GPUTexturePtr                       m_CookTorranceCompPdfScaleTexture;
    GPUTexturePtr                       m_CookTorranceCompEFresnelTexture;
    GPUTexturePtr                       m_CookTorranceBSDFETexture;
    GPUTexturePtr                       m_CookTorranceBSDFAvgETexture;
    GPUTexturePtr                       m_CookTorranceBTDFETexture;
    GPUTexturePtr                       m_CookTorranceBSDFInvCDFTexture;
    GPUTexturePtr                       m_CookTorranceBSDFPDFScaleTexture;
    GPUBufferPtr                        m_RayTracingFrameConstantBuffer;

    template <typename T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;
    ComPtr<ID3D11SamplerState>          m_UVClampSamplerState;
};