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

    LightingContext lightingContext = LightingContextInit( wo, wi );

    float etaI = invert ? intersection.ior : 1.0f;
    float etaT = invert ? 1.0f : intersection.ior;

    float transmissionWeight = intersection.transmission;
    float opaqueWeight = 1.0f - transmissionWeight;

    float E = SampleCookTorranceMicrofacetBRDFEnergyTexture( wo.z, intersection.alpha );
    float EAvg = SampleCookTorranceMicrofacetBRDFAverageEnergyTexture( intersection.alpha );
    float Fms = MultiscatteringFresnel( intersection.ior, EAvg );
    float cosThetaO = wo.z;
    float specularWeight        = SpecularWeight( cosThetaO, intersection.alpha, intersection.ior );
    float specularCompWeight    = Fms * ( 1.0f - E ) * opaqueWeight;
    float diffuseWeight = 1.0f - specularWeight - specularCompWeight;
    float3 value = EvaluateCookTorranceMircofacetBRDF( wi, wo, intersection.specular, intersection.alpha, etaI, etaT, lightingContext );
    value += EvaluateCookTorranceMultiscatteringBRDF( wi, wo, intersection.specular, intersection.alpha, E, EAvg, Fms );
    value += EvaluateLambertBRDF( wi, wo, intersection.albedo, intersection.backface ) * diffuseWeight;
    value = value * opaqueWeight + EvaluateCookTorranceMicrofacetMultiscatteringBSDF( wi, wo, intersection.specular, intersection.alpha, etaI, etaT, lightingContext ) * transmissionWeight;

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

    LightingContext lightingContext = LightingContextInit( wo, wi );

    float etaI = invert ? intersection.ior : 1.0f;
    float etaT = invert ? 1.0f : intersection.ior;

    float transmissionWeight = intersection.transmission;
    float opaqueWeight = 1.0f - transmissionWeight;

    float E = SampleCookTorranceMicrofacetBRDFEnergyTexture( wo.z, intersection.alpha );
    float EAvg = SampleCookTorranceMicrofacetBRDFAverageEnergyTexture( intersection.alpha );
    float Fms = MultiscatteringFresnel( intersection.ior, EAvg );
    float cosThetaO = wo.z;
    float specularWeight = SpecularWeight( cosThetaO, intersection.alpha, intersection.ior );
    float specularCompWeight = Fms * ( 1.0f - E ) * opaqueWeight;
    float diffuseWeight = 1.0f - specularWeight - specularCompWeight;

    float pdf = EvaluateCookTorranceMicrofacetBRDFPdf( wi, wo, intersection.alpha, lightingContext ) * specularWeight;
    pdf += EvaluateCookTorranceMultiscatteringBRDFPdf( wi, wo, intersection.alpha ) * specularCompWeight;
    pdf += EvaluateLambertBRDFPdf( wi, wo ) * diffuseWeight;
    pdf = pdf * opaqueWeight + EvaluateCookTorranceMicrofacetMultiscatteringBSDFPdf( wi, wo, intersection.alpha, etaI, etaT, lightingContext ) * transmissionWeight;

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

    LightingContext lightingContext = LightingContextInit( wo );

    float etaI = invert ? intersection.ior : 1.0f;
    float etaT = invert ? 1.0f : intersection.ior;

    float transmissionWeight = intersection.transmission;
    float opaqueWeight = 1.0f - transmissionWeight;
    float cosThetaO = wo.z;
    float E = SampleCookTorranceMicrofacetBRDFEnergyTexture( cosThetaO, intersection.alpha );
    float EAvg = SampleCookTorranceMicrofacetBRDFAverageEnergyTexture( intersection.alpha );
    float Fms = MultiscatteringFresnel( intersection.ior, EAvg );
    float specularWeight = SpecularWeight( cosThetaO, intersection.alpha, intersection.ior ) * opaqueWeight;
    float specularCompWeight = Fms * ( 1.0f - E ) * opaqueWeight;
    float diffuseWeight = ( 1.0f - specularWeight - specularCompWeight ) * opaqueWeight;
    
    isDeltaBxdf = false;
    if ( BRDFSelectionSample < specularWeight )
    {
        SampleCookTorranceMicrofacetBRDF( wo, BRDFSample, intersection.specular, intersection.alpha, etaI, etaT, wi, value, pdf, isDeltaBxdf, lightingContext );
        value *= opaqueWeight;
        pdf *= specularWeight;

        value += EvaluateCookTorranceMultiscatteringBRDF( wi, wo, intersection.specular, intersection.alpha, E, EAvg, Fms ) * opaqueWeight;
        pdf += EvaluateCookTorranceMultiscatteringBRDFPdf( wi, wo, intersection.alpha ) * specularCompWeight;

        value += EvaluateLambertBRDF( wi, wo, intersection.albedo, intersection.backface ) * diffuseWeight;
        pdf += EvaluateLambertBRDFPdf( wi, wo ) * diffuseWeight;
    }
    else if ( BRDFSelectionSample < specularWeight + specularCompWeight )
    {
        SampleCookTorranceMultiscatteringBRDF( wo, BRDFSample, intersection.specular, intersection.alpha, E, EAvg, Fms, wi, value, pdf, lightingContext );
        value *= opaqueWeight;
        pdf *= specularCompWeight;

        value += EvaluateCookTorranceMircofacetBRDF( wi, wo, intersection.specular, intersection.alpha, etaI, etaT, lightingContext ) * opaqueWeight;
        pdf += EvaluateCookTorranceMicrofacetBRDFPdf( wi, wo, intersection.alpha, lightingContext ) * specularWeight;

        value += EvaluateLambertBRDF( wi, wo, intersection.albedo, intersection.backface ) * diffuseWeight;
        pdf += EvaluateLambertBRDFPdf( wi, wo ) * diffuseWeight;
    }
    else if ( BRDFSelectionSample < opaqueWeight )
    {
        SampleLambertBRDF( wo, BRDFSample, intersection.albedo, intersection.backface, wi, value, pdf, lightingContext );
        value *= diffuseWeight;
        pdf *= diffuseWeight;

        value += EvaluateCookTorranceMircofacetBRDF( wi, wo, intersection.specular, intersection.alpha, etaI, etaT, lightingContext ) * opaqueWeight;
        pdf += EvaluateCookTorranceMicrofacetBRDFPdf( wi, wo, intersection.alpha, lightingContext ) * specularWeight;

        value += EvaluateCookTorranceMultiscatteringBRDF( wi, wo, intersection.specular, intersection.alpha, E, EAvg, Fms ) * opaqueWeight;
        pdf += EvaluateCookTorranceMultiscatteringBRDFPdf( wi, wo, intersection.alpha ) * specularCompWeight;
    }
    else
    {
        float sample = ( BRDFSelectionSample - opaqueWeight ) / transmissionWeight;
        SampleCookTorranceMicrofacetMultiscatteringBSDF( wo, sample, BRDFSample, intersection.specular, intersection.alpha, etaI, etaT, wi, value, pdf, isDeltaBxdf, lightingContext );
        value *= transmissionWeight;
        pdf *= transmissionWeight;
    }

    wi = mul( wi, tbn2world );
}

#endif