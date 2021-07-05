#pragma once

namespace BxDFTexturesBuilder
{
    GPUTexture* CreateCoorkTorranceBRDFEnergyTexture();

    GPUTexture* CreateCoorkTorranceBRDFEnergyFresnelDielectricTexture();

    GPUTexture* CreateCookTorranceBSDFEnergyFresnelDielectricTexture();

    GPUTexture* CreateCookTorranceBRDFAverageEnergyTexture();

    GPUTexture* CreateCookTorranceBSDFAverageEnergyTexture();

    GPUTexture* CreateCookTorranceBTDFEnergyTexture();

    GPUTexture* CreateCookTorranceMultiscatteringBRDFInvCDFTexture();

    GPUTexture* CreateCookTorranceMultiscatteringBRDFPDFScaleTexture();

    GPUTexture* CreateCookTorranceBSDFMultiscatteringInvCDFTexture();

    GPUTexture* CreateCookTorranceBSDFMultiscatteringPDFScaleTexture();
}