#ifndef _LAMBERTBRDF_H_
#define _LAMBERTBRDF_H_

#include "MonteCarlo.inc.hlsl"
#include "Math.inc.hlsl"
#include "LightingContext.inc.hlsl"

float3 ConsineSampleHemisphere( float2 sample )
{
    sample = ConcentricSampleDisk( sample );
    return float3( sample.xy, sqrt( max( 0.0f, 1.0f - dot( sample.xy, sample.xy ) ) ) );
}

float3 EvaluateLambertBRDF( float3 wi, float3 wo, float3 albedo, bool backface )
{
    return !backface ? albedo * INV_PI : 0.0f;
}

float EvaluateLambertBRDFPdf( float3 wi, LightingContext lightingContext )
{
    return max( 0.0f, lightingContext.WIdotN * INV_PI );
}

void SampleLambertBRDF( float3 wo
	, float2 sample
    , float3 albedo
    , bool backface
    , out float3 wi
    , out float3 value
    , out float pdf
    , inout LightingContext lightingContext )
{
    wi = ConsineSampleHemisphere( sample );

    lightingContext.WIdotN = wi.z;
    lightingContext.m = wi + wo;
    lightingContext.m = all( lightingContext.m == 0.0f ) ? 0.0f : normalize( lightingContext.m );

    value = EvaluateLambertBRDF( wi, wo, albedo, backface );
    pdf = EvaluateLambertBRDFPdf( wi, lightingContext );
}

#endif