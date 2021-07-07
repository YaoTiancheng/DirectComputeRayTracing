#ifndef _BSDF_H_
#define _BSDF_H_

#include "LambertBRDF.inc.hlsl"
#include "CookTorranceCompensationBRDF.inc.hlsl"
#include "CookTorranceBSDF.inc.hlsl"
#include "Intersection.inc.hlsl"
#include "LightingContext.inc.hlsl"

float SpecularWeight( float cosTheta, float alpha, float ior )
{
    return EvaluateCookTorranceCompEFresnel( cosTheta, alpha, ior );
}

float SpecularCompWeight( float ior, float E, float EAvg )
{
    return EvaluateCookTorranceCompFresnel( ior, EAvg ) * ( 1.0f - E );
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

    float3 value = EvaluateCookTorranceMircofacetBRDF( wi, wo, intersection.specular, intersection.alpha, etaI, etaT, lightingContext );
    value += EvaluateCookTorranceCompBRDF( wi, wo, intersection.specular, intersection.alpha, intersection.ior, lightingContext );

    float E = EvaluateCookTorranceCompE( lightingContext.WOdotN, intersection.alpha );
    float EAvg = EvaluateCookTorranceCompEAvg( intersection.alpha );
    float cosThetaO = lightingContext.WOdotN;
    float specularWeight        = SpecularWeight( cosThetaO, intersection.alpha, intersection.ior );
    float specularCompWeight    = SpecularCompWeight( intersection.ior, E, EAvg );
    float diffuseWeight = 1.0f - specularWeight - specularCompWeight;
    value += EvaluateLambertBRDF( wi, wo, intersection.albedo, intersection.backface ) * diffuseWeight;

    float transmissionWeight = intersection.transmission;
    float opaqueWeight = 1.0f - transmissionWeight;
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

    float E = EvaluateCookTorranceCompE( lightingContext.WOdotN, intersection.alpha );
    float EAvg = EvaluateCookTorranceCompEAvg( intersection.alpha );
    float cosThetaO = lightingContext.WOdotN;
    float specularWeight = SpecularWeight( cosThetaO, intersection.alpha, intersection.ior );
    float specularCompWeight = SpecularCompWeight( intersection.ior, E, EAvg );
    float diffuseWeight = 1.0f - specularWeight - specularCompWeight;

    float pdf = EvaluateCookTorranceMicrofacetBRDFPdf( wi, wo, intersection.alpha, lightingContext ) * specularWeight;
    pdf += EvaluateCookTorranceCompPdf( wi, intersection.alpha, lightingContext ) * specularCompWeight;
    pdf += EvaluateLambertBRDFPdf( lightingContext ) * diffuseWeight;

    float transmissionWeight = intersection.transmission;
    float opaqueWeight = 1.0f - transmissionWeight;
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
    float cosThetaO = lightingContext.WOdotN;
    float E = EvaluateCookTorranceCompE( cosThetaO, intersection.alpha );
    float EAvg = EvaluateCookTorranceCompEAvg( intersection.alpha );
    float specularWeight = SpecularWeight( cosThetaO, intersection.alpha, intersection.ior ) * opaqueWeight;
    float specularCompWeight = SpecularCompWeight( intersection.ior, E, EAvg ) * opaqueWeight;
    float diffuseWeight = ( 1.0f - specularWeight - specularCompWeight ) * opaqueWeight;
    
    isDeltaBxdf = false;
    if ( BRDFSelectionSample < specularWeight )
    {
        SampleCookTorranceMicrofacetBRDF( wo, BRDFSample, intersection.specular, intersection.alpha, etaI, etaT, wi, value, pdf, isDeltaBxdf, lightingContext );
        value *= opaqueWeight;
        pdf *= specularWeight;

        value += EvaluateCookTorranceCompBRDF( wi, wo, intersection.specular, intersection.alpha, intersection.ior, lightingContext ) * opaqueWeight;
        pdf += EvaluateCookTorranceCompPdf( wi, intersection.alpha, lightingContext ) * specularCompWeight;

        value += EvaluateLambertBRDF( wi, wo, intersection.albedo, intersection.backface ) * diffuseWeight;
        pdf += EvaluateLambertBRDFPdf( lightingContext ) * diffuseWeight;
    }
    else if ( BRDFSelectionSample < specularWeight + specularCompWeight )
    {
        SampleCookTorranceCompBRDF( wo, BRDFSample, intersection.specular, intersection.alpha, intersection.ior, wi, value, pdf, lightingContext );
        value *= opaqueWeight;
        pdf *= specularCompWeight;

        value += EvaluateCookTorranceMircofacetBRDF( wi, wo, intersection.specular, intersection.alpha, etaI, etaT, lightingContext ) * opaqueWeight;
        pdf += EvaluateCookTorranceMicrofacetBRDFPdf( wi, wo, intersection.alpha, lightingContext ) * specularWeight;

        value += EvaluateLambertBRDF( wi, wo, intersection.albedo, intersection.backface ) * diffuseWeight;
        pdf += EvaluateLambertBRDFPdf( lightingContext ) * diffuseWeight;
    }
    else if ( BRDFSelectionSample < opaqueWeight )
    {
        SampleLambertBRDF( wo, BRDFSample, intersection.albedo, intersection.backface, wi, value, pdf, lightingContext );
        value *= diffuseWeight;
        pdf *= diffuseWeight;

        value += EvaluateCookTorranceMircofacetBRDF( wi, wo, intersection.specular, intersection.alpha, etaI, etaT, lightingContext ) * opaqueWeight;
        pdf += EvaluateCookTorranceMicrofacetBRDFPdf( wi, wo, intersection.alpha, lightingContext ) * specularWeight;

        value += EvaluateCookTorranceCompBRDF( wi, wo, intersection.specular, intersection.alpha, intersection.ior, lightingContext ) * opaqueWeight;
        pdf += EvaluateCookTorranceCompPdf( wi, intersection.alpha, lightingContext ) * specularCompWeight;
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