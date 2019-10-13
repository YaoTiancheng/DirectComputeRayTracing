#ifndef _BSDF_H_
#define _BSDF_H_

#include "LambertBRDF.inc.hlsl"
#include "CookTorranceBRDF.inc.hlsl"
#include "CookTorranceCompensationBRDF.inc.hlsl"
#include "Primitives.inc.hlsl"

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

float4 EvaluateBSDF( float4 wi, float4 wo, Intersection intersection )
{
    float4 biNormal = float4( cross( intersection.tangent.xyz, intersection.normal.xyz ), 0.0f );
    float4x4 tbn2world = float4x4( intersection.tangent, biNormal, intersection.normal, float4( 0.0f, 0.0f, 0.0f, 1.0f ) );
    float4x4 world2tbn = transpose( tbn2world );

    wo = mul( wo, world2tbn );
    wi = mul( wi, world2tbn );

    float4 value = EvaluateCookTorranceMircofacetBRDF( wi, wo, intersection.specular, intersection.alpha, 1.0f, intersection.ior );
    value += EvaluateCookTorranceCompBRDF( wi, wo, intersection.specular, intersection.alpha, intersection.ior );

    float E = EvaluateCookTorranceCompE( wo.z, intersection.alpha );
    float EAvg = EvaluateCookTorranceCompEAvg( intersection.alpha );
    float cosThetaO = wo.z;
    float specularWeight        = SpecularWeight( cosThetaO, intersection.alpha, intersection.ior );
    float specularCompWeight    = SpecularCompWeight( intersection.ior, E, EAvg );
    float diffuseWeight = 1.0f - specularWeight - specularCompWeight;
    value += EvaluateLambertBRDF( wi, intersection.albedo ) * diffuseWeight;

    return value;
}

void SampleBSDF( float4 wo
    , float2 BRDFSample
    , float BRDFSelectionSample
    , Intersection intersection
    , out float4 wi
    , out float4 value
    , out float pdf )
{
    float4 biNormal = float4( cross( intersection.tangent.xyz, intersection.normal.xyz ), 0.0f );
    float4x4 tbn2world = float4x4( intersection.tangent, biNormal, intersection.normal, float4( 0.0f, 0.0f, 0.0f, 1.0f ) );
    float4x4 world2tbn = transpose( tbn2world );

    wo = mul( wo, world2tbn );

    bool invert = wo.z < 0.0f;
    float etaI = invert ? intersection.ior : 1.0f;
    float etaT = invert ? 1.0f : intersection.ior;

    float cosThetaO = wo.z;
    float E = EvaluateCookTorranceCompE( cosThetaO, intersection.alpha );
    float EAvg = EvaluateCookTorranceCompEAvg( intersection.alpha );
    float specularWeight = SpecularWeight( cosThetaO, intersection.alpha, intersection.ior );
    float specularCompWeight = SpecularCompWeight( intersection.ior, E, EAvg );
    float diffuseWeight = 1.0f - specularWeight - specularCompWeight;
    if ( BRDFSelectionSample < specularWeight )
    {
        SampleCookTorranceMicrofacetBRDF( wo, BRDFSample, intersection.specular, intersection.alpha, etaI, etaT, wi, value, pdf );
        pdf *= specularWeight;

        value += EvaluateCookTorranceCompBRDF( wi, wo, intersection.specular, intersection.alpha, intersection.ior );
        pdf += EvaluateCookTorranceCompPdf( wi, intersection.alpha ) * specularCompWeight;

        value += EvaluateLambertBRDF( wi, intersection.albedo ) * diffuseWeight;
        pdf += EvaluateLambertBRDFPdf( wi ) * diffuseWeight;
    }
    else if ( BRDFSelectionSample < specularWeight + specularCompWeight )
    {
        SampleCookTorranceCompBRDF( wo, BRDFSample, intersection.specular, intersection.alpha, intersection.ior, wi, value, pdf );
        pdf *= specularCompWeight;

        value += EvaluateCookTorranceMircofacetBRDF( wi, wo, intersection.specular, intersection.alpha, etaI, etaT );
        pdf += EvaluateCookTorranceMicrofacetBRDFPdf( wi, wo, intersection.alpha ) * specularWeight;

        value += EvaluateLambertBRDF( wi, intersection.albedo ) * diffuseWeight;
        pdf += EvaluateLambertBRDFPdf( wi ) * diffuseWeight;
    }
    else
    {
        SampleLambertBRDF( wo, BRDFSample, intersection.albedo, wi, value, pdf );
        value *= diffuseWeight;
        pdf *= diffuseWeight;

        value += EvaluateCookTorranceMircofacetBRDF( wi, wo, intersection.specular, intersection.alpha, etaI, etaT );
        pdf += EvaluateCookTorranceMicrofacetBRDFPdf( wi, wo, intersection.alpha ) * specularWeight;

        value += EvaluateCookTorranceCompBRDF( wi, wo, intersection.specular, intersection.alpha, intersection.ior );
        pdf += EvaluateCookTorranceCompPdf( wi, intersection.alpha ) * specularCompWeight;
    }

    wi = mul( wi, tbn2world );
}

#endif