#pragma once

namespace BxDFTexturesBuilder
{
    GPUTexture* CreateCoorkTorranceBRDFEnergyTexture();

    GPUTexture* CreateCoorkTorranceBRDFEnergyFresnelDielectricTexture();

    GPUTexture* CreateCookTorranceBSDFEnergyFresnelDielectricTexture();

    GPUTexture* CreateCookTorranceBRDFAverageEnergyTexture();

    GPUTexture* CreateCookTorranceMultiscatteringBRDFInvCDFTexture();

    GPUTexture* CreateCookTorranceMultiscatteringBRDFPDFScaleTexture();
}