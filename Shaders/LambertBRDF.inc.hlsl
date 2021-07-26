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

float EvaluateLambertBRDFPdf( float3 wi, float3 wo )
{
    return max( 0.0f, wi.z * INV_PI );
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

    LightingContextCalculateH( wo, wi, lightingContext );

    value = EvaluateLambertBRDF( wi, wo, albedo, backface );
    pdf = EvaluateLambertBRDFPdf( wi, wo );
}

#endif