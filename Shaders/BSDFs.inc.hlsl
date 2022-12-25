#ifndef _BSDF_H_
#define _BSDF_H_

#include "LambertBRDF.inc.hlsl"
#include "CookTorranceBSDF.inc.hlsl"
#include "Intersection.inc.hlsl"
#include "LightingContext.inc.hlsl"

float SpecularWeight( float cosTheta, float alpha, float ior )
{
    return SampleCookTorranceMicrofacetBRDFEnergyFresnelDielectricTexture( cosTheta, alpha, ior );
}

//
// BSDF
// 

float3 EvaluateBSDF( float3 wi, float3 wo, Intersection intersection )
{
    float3 biNormal = cross( intersection.normal, intersection.tangent );
    float3x3 tbn2world = float3x3( intersection.tangent, biNormal, intersection.normal );
    float3x3 world2tbn = transpose( tbn2world );

    wo = mul( wo, world2tbn );
    wi = mul( wi, world2tbn );

    bool isInverted = wo.z < 0.0f;
    if ( isInverted )
    {
        wo.z = -wo.z;
        wi.z = -wi.z;
    }

    LightingContext lightingContext = LightingContextInit( wo, wi, isInverted );

    float3 etaI = 1.0f;
    float3 etaT = intersection.ior;
    float3 eta = etaT / etaI;

    bool isMetal = intersection.isMetal;

    float transmission = intersection.transmission;
    float opacity     = 1.0f - transmission;

    float E           = SampleCookTorranceMicrofacetBRDFEnergyTexture( wo.z, intersection.alpha );
    float EAvg        = SampleCookTorranceMicrofacetBRDFAverageEnergyTexture( intersection.alpha );
    float3 Favg       = !isMetal ? MultiscatteringFavgDielectric( eta.r ) : MultiscatteringFavgConductor( intersection.ior, intersection.k );
    float3 Fms        = MultiscatteringFresnel( EAvg, Favg );
    float cosThetaO   = wo.z;
    // Energy for conductor microfacet BRDF is not available, hence we cannot importance sample between microfacet and multiscattering
    // and assume both energy is 0.5 in such case
    float Emicrofacet = !isMetal ? SpecularWeight( cosThetaO, intersection.alpha, intersection.ior.r ) : 0.5f;
          Emicrofacet *= opacity;
    float Ems         = !isMetal ? Fms.r * ( 1.0f - E ) : 0.5f;
          Ems         *= opacity;
    float Ediffuse    = !isMetal ? ( 1 - Emicrofacet - Ems ) : 0.0f;
    float Et          = transmission;
    float3 value = 0.0f;
    bool hasAnyBrdf = !isInverted || intersection.isTwoSided;
    if ( hasAnyBrdf )
    {
        value += !isMetal ? EvaluateCookTorranceMircofacetBRDF_Dielectric( wi, wo, intersection.specular, intersection.alpha, etaI.r, etaT.r, lightingContext )
                          : EvaluateCookTorranceMircofacetBRDF_Conductor( wi, wo, intersection.specular, intersection.alpha, 1.0f, intersection.ior, intersection.k, lightingContext );
        value += EvaluateCookTorranceMultiscatteringBRDF( wi, wo, intersection.specular, intersection.alpha, E, EAvg, Fms, lightingContext );
        value += EvaluateLambertBRDF( wi, wo, intersection.albedo, lightingContext ) * Ediffuse;
        value = value * opacity;
    }
    if ( transmission > 0.0f )
    {
        float etaI_t = isInverted ? intersection.ior.r : 1.0f;
        float etaT_t = isInverted ? 1.0f : intersection.ior.r;
        value += EvaluateCookTorranceMicrofacetMultiscatteringBSDF( wi, wo, intersection.specular, intersection.alpha, etaI_t, etaT_t, lightingContext ) * Et;
    }

    return value;
}

float EvaluateBSDFPdf( float3 wi, float3 wo, Intersection intersection )
{
    float3 biNormal = cross( intersection.normal, intersection.tangent );
    float3x3 tbn2world = float3x3( intersection.tangent, biNormal, intersection.normal );
    float3x3 world2tbn = transpose( tbn2world );

    wo = mul( wo, world2tbn );
    wi = mul( wi, world2tbn );

    bool isInverted = wo.z < 0.0f;
    if ( isInverted )
    {
        wo.z = -wo.z;
        wi.z = -wi.z;
    }

    LightingContext lightingContext = LightingContextInit( wo, wi, isInverted );

    float3 etaI = 1.0f;
    float3 etaT = intersection.ior;
    float3 eta = etaT / etaI;

    bool isMetal = intersection.isMetal;

    float transmission = intersection.transmission;
    float opacity     = 1.0f - transmission;

    float E           = SampleCookTorranceMicrofacetBRDFEnergyTexture( wo.z, intersection.alpha );
    float EAvg        = SampleCookTorranceMicrofacetBRDFAverageEnergyTexture( intersection.alpha );
    float3 Favg       = !isMetal ? MultiscatteringFavgDielectric( eta.r ) : MultiscatteringFavgConductor( intersection.ior, intersection.k );
    float3 Fms        = MultiscatteringFresnel( EAvg, Favg );
    float cosThetaO   = wo.z;

    bool hasAnyBrdf = !isInverted || intersection.isTwoSided;
    // Energy for conductor microfacet BRDF is not available, hence we cannot importance sample between microfacet and multiscattering
    // and assume both energy is 0.5 in such case
    float Emicrofacet = !isMetal ? SpecularWeight( cosThetaO, intersection.alpha, intersection.ior.r ) : 0.5f;
          Emicrofacet *= opacity;
          Emicrofacet *= hasAnyBrdf ? 1.0f : 0.0f;
    float Ems         = !isMetal ? Fms.r * ( 1.0f - E ) : 0.5f;
          Ems         *= opacity;
          Ems         *= hasAnyBrdf ? 1.0f : 0.0f;
    float Ediffuse    = !isMetal ? ( 1 - Emicrofacet - Ems ) * opacity : 0.0f;
          Ediffuse    *= hasAnyBrdf ? 1.0f : 0.0f;
    float Et          = transmission;
    float Etotal      = Emicrofacet + Ems + Ediffuse + Et;
    if ( Etotal == 0.0f )
    {
        return 0.0f;
    }

    float Wmicrofacet = Emicrofacet / Etotal;
    float Wms         = Ems / Etotal;
    float Wdiffuse    = Ediffuse / Etotal;
    float Wt          = Et / Etotal;

    float pdf = 0.0f;
    if ( hasAnyBrdf )
    {
        pdf += EvaluateCookTorranceMicrofacetBRDFPdf( wi, wo, intersection.alpha, lightingContext ) * Wmicrofacet;
        pdf += EvaluateCookTorranceMultiscatteringBRDFPdf( wi, wo, intersection.alpha, lightingContext ) * Wms;
        pdf += EvaluateLambertBRDFPdf( wi, wo, lightingContext ) * Wdiffuse;
    }
    if ( transmission > 0.0f )
    {
        float etaI_t = isInverted ? intersection.ior.r : 1.0f;
        float etaT_t = isInverted ? 1.0f : intersection.ior.r;
        pdf += EvaluateCookTorranceMicrofacetMultiscatteringBSDFPdf( wi, wo, intersection.alpha, etaI_t, etaT_t, lightingContext ) * Wt;
    }

    return pdf;
}

void SampleBSDF( float3 wo
    , float2 BRDFSample
    , float BRDFSelectionSample
    , Intersection intersection
    , out float3 wi
    , out float3 value
    , out float pdf
    , out bool isDeltaBxdf )
{
    wi = 0.0f;
    value = 0.0f;
    pdf = 0.0f;
    isDeltaBxdf = false;

    float3 biNormal = cross( intersection.normal, intersection.tangent );
    float3x3 tbn2world = float3x3( intersection.tangent, biNormal, intersection.normal );
    float3x3 world2tbn = transpose( tbn2world );

    wo = mul( wo, world2tbn );

    bool isInverted = wo.z < 0.0f;
    if ( isInverted )
    {
        wo.z = -wo.z;
    }

    LightingContext lightingContext = LightingContextInit( wo, isInverted );

    float3 etaI = 1.0f;
    float3 etaT = intersection.ior;
    float3 eta = etaT / etaI;
    float etaI_t = isInverted ? intersection.ior.r : 1.0f;
    float etaT_t = isInverted ? 1.0f : intersection.ior.r;

    bool isMetal      = intersection.isMetal;
    float transmission = intersection.transmission;
    float opacity     = 1.0f - transmission;
    float cosThetaO   = wo.z;
    float E           = SampleCookTorranceMicrofacetBRDFEnergyTexture( cosThetaO, intersection.alpha );
    float EAvg        = SampleCookTorranceMicrofacetBRDFAverageEnergyTexture( intersection.alpha );
    float3 Favg       = !isMetal ? MultiscatteringFavgDielectric( eta.r ) : MultiscatteringFavgConductor( intersection.ior, intersection.k );
    float3 Fms        = MultiscatteringFresnel( EAvg, Favg );

    bool hasAnyBrdf = !isInverted || intersection.isTwoSided;
    // Energy for conductor microfacet BRDF is not available, hence we cannot importance sample between microfacet and multiscattering
    // and assume both energy is 0.5 in such case
    float Emicrofacet = !isMetal ? SpecularWeight( cosThetaO, intersection.alpha, intersection.ior.r ) : 0.5f;
          Emicrofacet *= opacity;
          Emicrofacet *= hasAnyBrdf ? 1.0f : 0.0f;
    float Ems         = !isMetal ? Fms.r * ( 1.0f - E ) : 0.5f;
          Ems         *= opacity;
          Ems         *= hasAnyBrdf ? 1.0f : 0.0f;
    float Ediffuse    = !isMetal ? ( 1 - Emicrofacet - Ems ) * opacity : 0.0f;
          Ediffuse    *= hasAnyBrdf ? 1.0f : 0.0f;
    float Et          = transmission;
    float Etotal      = Emicrofacet + Ems + Ediffuse + Et;
    if ( Etotal == 0.0f )
    {
        return;
    }

    float Wmicrofacet = Emicrofacet / Etotal;
    float Wms         = Ems / Etotal;
    float Wdiffuse    = Ediffuse / Etotal;
    float Wt          = Et / Etotal;

    static const uint BxDF_INDEX_LAMBERT_BRDF = 0;
    static const uint BxDF_INDEX_COOKTORRANCE_MICROFACET_BRDF = 1;
    static const uint BxDF_INDEX_COOKTORRANCE_MULTISCATTERING_BRDF = 2;
    static const uint BxDF_INDEX_COOKTORRANCE_BSDF = 3;

    uint bxdfIndex = 0;
    if ( BRDFSelectionSample < Wmicrofacet )
        bxdfIndex = BxDF_INDEX_COOKTORRANCE_MICROFACET_BRDF;
    else if ( BRDFSelectionSample < Wmicrofacet + Wms )
        bxdfIndex = BxDF_INDEX_COOKTORRANCE_MULTISCATTERING_BRDF;
    else if ( BRDFSelectionSample < Wmicrofacet + Wms + Wdiffuse )
        bxdfIndex = BxDF_INDEX_LAMBERT_BRDF;
    else 
        bxdfIndex = BxDF_INDEX_COOKTORRANCE_BSDF;

    isDeltaBxdf = false;
    switch ( bxdfIndex )
    {
    case BxDF_INDEX_COOKTORRANCE_MICROFACET_BRDF:
    {
        if ( !isMetal ) SampleCookTorranceMicrofacetBRDF_Dielectric( wo, BRDFSample, intersection.specular, intersection.alpha, etaI.r, etaT.r, wi, value, pdf, isDeltaBxdf, lightingContext );
        else SampleCookTorranceMicrofacetBRDF_Conductor( wo, BRDFSample, intersection.specular, intersection.alpha, 1.0f, intersection.ior, intersection.k, wi, value, pdf, isDeltaBxdf, lightingContext );
        value *= opacity;
        pdf *= Wmicrofacet;
        break;
    }
    case BxDF_INDEX_COOKTORRANCE_MULTISCATTERING_BRDF:
    {
        SampleCookTorranceMultiscatteringBRDF( wo, BRDFSample, intersection.specular, intersection.alpha, E, EAvg, Fms, wi, value, pdf, lightingContext );
        value *= opacity;
        pdf *= Wms;
        break;
    }
    case BxDF_INDEX_LAMBERT_BRDF:
    {
        SampleLambertBRDF( wo, BRDFSample, intersection.albedo, intersection.backface, wi, value, pdf, lightingContext );
        value *= Ediffuse;
        pdf *= Wdiffuse;
        break;
    }
    case BxDF_INDEX_COOKTORRANCE_BSDF:
    {
        float sample = ( BRDFSelectionSample - opacity ) / transmission;
        SampleCookTorranceMicrofacetMultiscatteringBSDF( wo, sample, BRDFSample, intersection.specular, intersection.alpha, etaI_t, etaT_t, wi, value, pdf, isDeltaBxdf, lightingContext );
        value *= Et;
        pdf *= Wt;
        break;
    }
    }

    if ( !isDeltaBxdf )
    {
        if ( hasAnyBrdf && bxdfIndex != BxDF_INDEX_COOKTORRANCE_MICROFACET_BRDF && opacity > 0.0f )
        {
            float3 brdf = !isMetal ? EvaluateCookTorranceMircofacetBRDF_Dielectric( wi, wo, intersection.specular, intersection.alpha, etaI.r, etaT.r, lightingContext )
                : EvaluateCookTorranceMircofacetBRDF_Conductor( wi, wo, intersection.specular, intersection.alpha, 1.0f, intersection.ior, intersection.k, lightingContext );
            value += brdf * opacity;
            pdf += EvaluateCookTorranceMicrofacetBRDFPdf( wi, wo, intersection.alpha, lightingContext ) * Wmicrofacet;
        }
        if ( hasAnyBrdf && bxdfIndex != BxDF_INDEX_COOKTORRANCE_MULTISCATTERING_BRDF && opacity > 0.0f )
        {
            value += EvaluateCookTorranceMultiscatteringBRDF( wi, wo, intersection.specular, intersection.alpha, E, EAvg, Fms, lightingContext ) * opacity;
            pdf += EvaluateCookTorranceMultiscatteringBRDFPdf( wi, wo, intersection.alpha, lightingContext ) * Wms;
        }
        if ( hasAnyBrdf && bxdfIndex != BxDF_INDEX_LAMBERT_BRDF && opacity > 0.0f )
        {
            value += EvaluateLambertBRDF( wi, wo, intersection.albedo, lightingContext ) * Ediffuse;
            pdf += EvaluateLambertBRDFPdf( wi, wo, lightingContext ) * Wdiffuse;
        }
        if ( bxdfIndex != BxDF_INDEX_COOKTORRANCE_BSDF && transmission > 0.0f )
        {
            value += EvaluateCookTorranceMicrofacetMultiscatteringBSDF( wi, wo, intersection.specular, intersection.alpha, etaI_t, etaT_t, lightingContext ) * Et;
            pdf += EvaluateCookTorranceMicrofacetMultiscatteringBSDFPdf( wi, wo, intersection.alpha, etaI_t, etaT_t, lightingContext ) * Wt;
        }
    }

    if ( isInverted )
    {
        wi.z = -wi.z;
    }

    wi = mul( wi, tbn2world );
}

#endif