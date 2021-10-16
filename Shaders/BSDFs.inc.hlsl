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
    float WOdotN = dot( wo, intersection.normal );
    bool invert = WOdotN < 0.0f;
    intersection.normal = invert ? -intersection.normal : intersection.normal;

    float3 biNormal = cross( intersection.normal, intersection.tangent );
    float3x3 tbn2world = float3x3( intersection.tangent, biNormal, intersection.normal );
    float3x3 world2tbn = transpose( tbn2world );

    wo = mul( wo, world2tbn );
    wi = mul( wi, world2tbn );

    LightingContext lightingContext = LightingContextInit( wo, wi, invert );

    float etaI = invert ? intersection.ior : 1.0f;
    float etaT = invert ? 1.0f : intersection.ior;
    float eta = etaT / etaI;

    bool isMetal = intersection.isMetal;

    float transmission = intersection.transmission;
    float opacity     = 1.0f - transmission;

    float E           = SampleCookTorranceMicrofacetBRDFEnergyTexture( wo.z, intersection.alpha );
    float EAvg        = SampleCookTorranceMicrofacetBRDFAverageEnergyTexture( intersection.alpha );
    float3 Favg       = !isMetal ? MultiscatteringFavgDielectric( eta ) : MultiscatteringFavgConductor( eta, intersection.k );
    float3 Fms        = MultiscatteringFresnel( EAvg, Favg );
    float cosThetaO   = wo.z;
    // Energy for conductor microfacet BRDF is not available, hence we cannot importance sample between microfacet and multiscattering
    // and assume both energy is 0.5 in such case
    float Emicrofacet = !isMetal ? SpecularWeight( cosThetaO, intersection.alpha, intersection.ior ) : 0.5f;
          Emicrofacet *= opacity;
    float Ems         = !isMetal ? Fms.r * ( 1.0f - E ) : 0.5f;
          Ems         *= opacity;
    float Ediffuse    = !isMetal ? ( 1 - Emicrofacet - Ems ) : 0.0f;
    float Et          = transmission;
    float3 value = !isMetal ? EvaluateCookTorranceMircofacetBRDF_Dielectric( wi, wo, intersection.specular, intersection.alpha, etaI, etaT, lightingContext )
                            : EvaluateCookTorranceMircofacetBRDF_Conductor( wi, wo, intersection.specular, intersection.alpha, etaI, etaT, intersection.k, lightingContext );
    value += EvaluateCookTorranceMultiscatteringBRDF( wi, wo, intersection.specular, intersection.alpha, E, EAvg, Fms, lightingContext );
    value += EvaluateLambertBRDF( wi, wo, intersection.albedo, lightingContext ) * Ediffuse;
    value = value * opacity + EvaluateCookTorranceMicrofacetMultiscatteringBSDF( wi, wo, intersection.specular, intersection.alpha, etaI, etaT, lightingContext ) * Et;

    return value;
}

float EvaluateBSDFPdf( float3 wi, float3 wo, Intersection intersection )
{
    float WOdotN = dot( wo, intersection.normal );
    bool invert = WOdotN < 0.0f;
    intersection.normal = invert ? -intersection.normal : intersection.normal;

    float3 biNormal = cross( intersection.normal, intersection.tangent );
    float3x3 tbn2world = float3x3( intersection.tangent, biNormal, intersection.normal );
    float3x3 world2tbn = transpose( tbn2world );

    wo = mul( wo, world2tbn );
    wi = mul( wi, world2tbn );

    LightingContext lightingContext = LightingContextInit( wo, wi, invert );

    float etaI = invert ? intersection.ior : 1.0f;
    float etaT = invert ? 1.0f : intersection.ior;
    float eta = etaT / etaI;

    bool isMetal = intersection.isMetal;

    float transmission = intersection.transmission;
    float opacity     = 1.0f - transmission;

    float E           = SampleCookTorranceMicrofacetBRDFEnergyTexture( wo.z, intersection.alpha );
    float EAvg        = SampleCookTorranceMicrofacetBRDFAverageEnergyTexture( intersection.alpha );
    float3 Favg       = !isMetal ? MultiscatteringFavgDielectric( eta ) : MultiscatteringFavgConductor( eta, intersection.k );
    float3 Fms        = MultiscatteringFresnel( EAvg, Favg );
    float cosThetaO   = wo.z;
    // Energy for conductor microfacet BRDF is not available, hence we cannot importance sample between microfacet and multiscattering
    // and assume both energy is 0.5 in such case
    float Emicrofacet = !isMetal ? SpecularWeight( cosThetaO, intersection.alpha, intersection.ior ) : 0.5f;
          Emicrofacet *= opacity;
    float Ems         = !isMetal ? Fms.r * ( 1.0f - E ) : 0.5f;
          Ems         *= opacity;
    float Ediffuse    = !isMetal ? ( 1 - Emicrofacet - Ems ) * opacity : 0.0f;
    float Et          = transmission;
    float Etotal      = Emicrofacet + Ems + Ediffuse + Et;
    float Wmicrofacet = Emicrofacet / Etotal;
    float Wms         = Ems / Etotal;
    float Wdiffuse    = Ediffuse / Etotal;
    float Wt          = Et / Etotal;

    float pdf = EvaluateCookTorranceMicrofacetBRDFPdf( wi, wo, intersection.alpha, lightingContext ) * Wmicrofacet;
    pdf += EvaluateCookTorranceMultiscatteringBRDFPdf( wi, wo, intersection.alpha, lightingContext ) * Wms;
    pdf += EvaluateLambertBRDFPdf( wi, wo, lightingContext ) * Wdiffuse;
    pdf += EvaluateCookTorranceMicrofacetMultiscatteringBSDFPdf( wi, wo, intersection.alpha, etaI, etaT, lightingContext ) * Wt;

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
    float WOdotN = dot( wo, intersection.normal );
    bool invert = WOdotN < 0.0f;
    intersection.normal = invert ? -intersection.normal : intersection.normal;

    float3 biNormal = cross( intersection.normal, intersection.tangent );
    float3x3 tbn2world = float3x3( intersection.tangent, biNormal, intersection.normal );
    float3x3 world2tbn = transpose( tbn2world );

    wo = mul( wo, world2tbn );

    LightingContext lightingContext = LightingContextInit( wo, invert );

    float etaI = invert ? intersection.ior : 1.0f;
    float etaT = invert ? 1.0f : intersection.ior;
    float eta = etaT / etaI;

    bool isMetal      = intersection.isMetal;
    float transmission = intersection.transmission;
    float opacity     = 1.0f - transmission;
    float cosThetaO   = wo.z;
    float E           = SampleCookTorranceMicrofacetBRDFEnergyTexture( cosThetaO, intersection.alpha );
    float EAvg        = SampleCookTorranceMicrofacetBRDFAverageEnergyTexture( intersection.alpha );
    float3 Favg       = !isMetal ? MultiscatteringFavgDielectric( eta ) : MultiscatteringFavgConductor( eta, intersection.k );
    float3 Fms        = MultiscatteringFresnel( EAvg, Favg );
    // Energy for conductor microfacet BRDF is not available, hence we cannot importance sample between microfacet and multiscattering
    // and assume both energy is 0.5 in such case
    float Emicrofacet = !isMetal ? SpecularWeight( cosThetaO, intersection.alpha, intersection.ior ) : 0.5f;
          Emicrofacet *= opacity;
    float Ems         = !isMetal ? Fms.r * ( 1.0f - E ) : 0.5f;
          Ems         *= opacity;
    float Ediffuse    = !isMetal ? ( 1 - Emicrofacet - Ems ) * opacity : 0.0f;
    float Et          = transmission;
    float Etotal      = Emicrofacet + Ems + Ediffuse + Et;
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
        if ( !isMetal ) SampleCookTorranceMicrofacetBRDF_Dielectric( wo, BRDFSample, intersection.specular, intersection.alpha, etaI, etaT, wi, value, pdf, isDeltaBxdf, lightingContext );
        else SampleCookTorranceMicrofacetBRDF_Conductor( wo, BRDFSample, intersection.specular, intersection.alpha, etaI, etaT, intersection.k, wi, value, pdf, isDeltaBxdf, lightingContext );
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
        SampleCookTorranceMicrofacetMultiscatteringBSDF( wo, sample, BRDFSample, intersection.specular, intersection.alpha, etaI, etaT, wi, value, pdf, isDeltaBxdf, lightingContext );
        value *= Et;
        pdf *= Wt;
        break;
    }
    }

    if ( bxdfIndex != BxDF_INDEX_COOKTORRANCE_MICROFACET_BRDF && opacity > 0.0f )
    {
        float3 brdf = !isMetal ? EvaluateCookTorranceMircofacetBRDF_Dielectric( wi, wo, intersection.specular, intersection.alpha, etaI, etaT, lightingContext )
                               : EvaluateCookTorranceMircofacetBRDF_Conductor( wi, wo, intersection.specular, intersection.alpha, etaI, etaT, intersection.k, lightingContext );
        value += brdf * opacity;
        pdf += EvaluateCookTorranceMicrofacetBRDFPdf( wi, wo, intersection.alpha, lightingContext ) * Wmicrofacet;
    }
    if ( bxdfIndex != BxDF_INDEX_COOKTORRANCE_MULTISCATTERING_BRDF && opacity > 0.0f )
    {
        value += EvaluateCookTorranceMultiscatteringBRDF( wi, wo, intersection.specular, intersection.alpha, E, EAvg, Fms, lightingContext ) * opacity;
        pdf += EvaluateCookTorranceMultiscatteringBRDFPdf( wi, wo, intersection.alpha, lightingContext ) * Wms;
    }
    if ( bxdfIndex != BxDF_INDEX_LAMBERT_BRDF && opacity > 0.0f )
    {
        value += EvaluateLambertBRDF( wi, wo, intersection.albedo, lightingContext ) * Ediffuse;
        pdf += EvaluateLambertBRDFPdf( wi, wo, lightingContext ) * Wdiffuse;
    }
    if ( bxdfIndex != BxDF_INDEX_COOKTORRANCE_BSDF && transmission > 0.0f )
    {
        value += EvaluateCookTorranceMicrofacetMultiscatteringBSDF( wi, wo, intersection.specular, intersection.alpha, etaI, etaT, lightingContext ) * Et;
        pdf += EvaluateCookTorranceMicrofacetMultiscatteringBSDFPdf( wi, wo, intersection.alpha, etaI, etaT, lightingContext ) * Wt;
    }

    wi = mul( wi, tbn2world );
}

#endif