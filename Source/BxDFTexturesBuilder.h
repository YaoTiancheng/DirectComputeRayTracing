#pragma once

namespace BxDFTexturesBuilder
{
    GPUTexture* CreateCoorkTorranceBRDFEnergyTexture();

    GPUTexture* CreateCoorkTorranceBRDFEnergyFresnelDielectricTexture();

    GPUTexture* CreateCoorkTorranceBRDFEnergyInverseFresnelDielectricTexture();

    GPUTexture* CreateCookTorranceBRDFAverageEnergyTexture();

    GPUTexture* CreateCookTorranceMultiscatteringBRDFInvCDFTexture();

    GPUTexture* CreateCookTorranceMultiscatteringBRDFPDFScaleTexture();
}