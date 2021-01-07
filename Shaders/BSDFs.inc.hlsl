#ifndef _BSDF_H_
#define _BSDF_H_

#include "LambertBRDF.inc.hlsl"
#include "CookTorranceBRDF.inc.hlsl"
#include "CookTorranceCompensationBRDF.inc.hlsl"
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
    float3 biNormal = cross( intersection.tangent, intersection.normal );
    float3x3 tbn2world = float3x3( intersection.tangent, biNormal, intersection.normal );
    float3x3 world2tbn = transpose( tbn2world );

    wo = mul( wo, world2tbn );
    wi = mul( wi, world2tbn );

    LightingContext lightingContext = LightingContextInit( wo, wi );

    float3 value = EvaluateCookTorranceMircofacetBRDF( wi, wo, intersection.specular, intersection.alpha, 1.0f, intersection.ior, lightingContext );
    value += EvaluateCookTorranceCompBRDF( wi, wo, intersection.specular, intersection.alpha, intersection.ior, lightingContext );

    float E = EvaluateCookTorranceCompE( lightingContext.WOdotN, intersection.alpha );
    float EAvg = EvaluateCookTorranceCompEAvg( intersection.alpha );
    float cosThetaO = lightingContext.WOdotN;
    float specularWeight        = SpecularWeight( cosThetaO, intersection.alpha, intersection.ior );
    float specularCompWeight    = SpecularCompWeight( intersection.ior, E, EAvg );
    float diffuseWeight = 1.0f - specularWeight - specularCompWeight;
    value += EvaluateLambertBRDF( wi, wo, intersection.albedo, intersection.backface ) * diffuseWeight;

    return value;
}

void SampleBSDF( float3 wo
    , float2 BRDFSample
    , float BRDFSelectionSample
    , Intersection intersection
    , out float3 wi
    , out float3 value
    , out float pdf )
{
    float3 biNormal = cross( intersection.tangent.xyz, intersection.normal.xyz );
    float3x3 tbn2world = float3x3( intersection.tangent, biNormal, intersection.normal );
    float3x3 world2tbn = transpose( tbn2world );

    wo = mul( wo, world2tbn );

    LightingContext lightingContext = LightingContextInit( wo );

    bool invert = lightingContext.WOdotN < 0.0f;
    float etaI = invert ? intersection.ior : 1.0f;
    float etaT = invert ? 1.0f : intersection.ior;

    float cosThetaO = lightingContext.WOdotN;
    float E = EvaluateCookTorranceCompE( cosThetaO, intersection.alpha );
    float EAvg = EvaluateCookTorranceCompEAvg( intersection.alpha );
    float specularWeight = SpecularWeight( cosThetaO, intersection.alpha, intersection.ior );
    float specularCompWeight = SpecularCompWeight( intersection.ior, E, EAvg );
    float diffuseWeight = 1.0f - specularWeight - specularCompWeight;
    if ( BRDFSelectionSample < specularWeight )
    {
        SampleCookTorranceMicrofacetBRDF( wo, BRDFSample, intersection.specular, intersection.alpha, etaI, etaT, wi, value, pdf, lightingContext );
        pdf *= specularWeight;

        value += EvaluateCookTorranceCompBRDF( wi, wo, intersection.specular, intersection.alpha, intersection.ior, lightingContext );
        pdf += EvaluateCookTorranceCompPdf( wi, intersection.alpha, lightingContext ) * specularCompWeight;

        value += EvaluateLambertBRDF( wi, wo, intersection.albedo, intersection.backface ) * diffuseWeight;
        pdf += EvaluateLambertBRDFPdf( wi, lightingContext ) * diffuseWeight;
    }
    else if ( BRDFSelectionSample < specularWeight + specularCompWeight )
    {
        SampleCookTorranceCompBRDF( wo, BRDFSample, intersection.specular, intersection.alpha, intersection.ior, wi, value, pdf, lightingContext );
        pdf *= specularCompWeight;

        value += EvaluateCookTorranceMircofacetBRDF( wi, wo, intersection.specular, intersection.alpha, etaI, etaT, lightingContext );
        pdf += EvaluateCookTorranceMicrofacetBRDFPdf( wi, wo, intersection.alpha, lightingContext ) * specularWeight;

        value += EvaluateLambertBRDF( wi, wo, intersection.albedo, intersection.backface ) * diffuseWeight;
        pdf += EvaluateLambertBRDFPdf( wi, lightingContext ) * diffuseWeight;
    }
    else
    {
        SampleLambertBRDF( wo, BRDFSample, intersection.albedo, intersection.backface, wi, value, pdf, lightingContext );
        value *= diffuseWeight;
        pdf *= diffuseWeight;

        value += EvaluateCookTorranceMircofacetBRDF( wi, wo, intersection.specular, intersection.alpha, etaI, etaT, lightingContext );
        pdf += EvaluateCookTorranceMicrofacetBRDFPdf( wi, wo, intersection.alpha, lightingContext ) * specularWeight;

        value += EvaluateCookTorranceCompBRDF( wi, wo, intersection.specular, intersection.alpha, intersection.ior, lightingContext );
        pdf += EvaluateCookTorranceCompPdf( wi, intersection.alpha, lightingContext ) * specularCompWeight;
    }

    wi = mul( wi, tbn2world );
}

#endif