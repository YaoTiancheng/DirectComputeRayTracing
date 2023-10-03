#pragma once

namespace BxDFTexturesBuilding
{
    struct STextures
    {
        GPUTexturePtr m_CookTorranceBRDF;
        GPUTexturePtr m_CookTorranceBRDFAverage;
    };

    STextures Build();
}