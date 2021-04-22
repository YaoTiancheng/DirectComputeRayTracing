#pragma once

namespace BxDFTexturesBuilder
{
    GPUTexture* CreateCoorkTorranceBRDFEnergyTexture();

    GPUTexture* CreateCoorkTorranceBRDFEnergyFresnelDielectricTexture();

    GPUTexture* CreateCookTorranceBRDFAverageEnergyTexture();

    GPUTexture* CreateCookTorranceMultiscatteringBRDFInvCDFTexture();

    GPUTexture* CreateCookTorranceMultiscatteringBRDFPDFScaleTexture();
}