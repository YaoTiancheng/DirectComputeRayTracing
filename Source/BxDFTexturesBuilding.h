#pragma once

namespace BxDFTexturesBuilding
{
    struct STextures
    {
        GPUTexturePtr m_CookTorranceBRDF;
        GPUTexturePtr m_CookTorranceBRDFAverage;
        GPUTexturePtr m_CookTorranceBRDFDielectric;
        GPUTexturePtr m_CookTorranceBSDF;
        GPUTexturePtr m_CookTorranceBSDFAverage;
    };

    STextures Build();
}