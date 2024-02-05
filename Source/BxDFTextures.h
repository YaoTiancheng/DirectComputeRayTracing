#pragma once

struct SBxDFTextures
{
    bool AllTexturesSet() const
    {
        return m_CookTorranceBRDF && m_CookTorranceBRDFAverage && m_CookTorranceBRDFDielectric && m_CookTorranceBSDF && m_CookTorranceBSDFAverage;
    }

    GPUTexturePtr m_CookTorranceBRDF;
    GPUTexturePtr m_CookTorranceBRDFAverage;
    GPUTexturePtr m_CookTorranceBRDFDielectric;
    GPUTexturePtr m_CookTorranceBSDF;
    GPUTexturePtr m_CookTorranceBSDFAverage;
};