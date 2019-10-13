#ifndef _LAMBERTBRDF_H_
#define _LAMBERTBRDF_H_

#include "MonteCarlo.inc.hlsl"
#include "Math.inc.hlsl"

float4 ConsineSampleHemisphere( float2 sample )
{
    sample = ConcentricSampleDisk( sample );
    return float4( sample.xy, sqrt( max( 0.0f, 1.0f - dot( sample.xy, sample.xy ) ) ), 0.0f );
}

float4 EvaluateLambertBRDF( float4 wi, float4 albedo )
{
    return wi.z > 0.0f ? albedo * INV_PI : 0.0f;
}

float EvaluateLambertBRDFPdf( float4 wi )
{
    return max( 0.0f, wi.z * INV_PI );
}

void SampleLambertBRDF( float4 wo
	, float2 sample
    , float4 albedo
    , out float4 wi
    , out float4 value
    , out float pdf )
{
    wi = ConsineSampleHemisphere( sample );
    value = EvaluateLambertBRDF( wi, albedo );
    pdf = EvaluateLambertBRDFPdf( wi );
}

#endif