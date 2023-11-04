#ifndef _LAMBERTBRDF_H_
#define _LAMBERTBRDF_H_

#include "MonteCarlo.inc.hlsl"
#include "Math.inc.hlsl"
#include "LightingContext.inc.hlsl"

float EvaluateLambertBRDF( float3 wi, float3 wo, LightingContext lightingContext )
{
    return wi.z > 0.0f && wo.z > 0.0f ? INV_PI : 0.0f;
}

float EvaluateLambertBRDFPdf( float3 wi, float3 wo, LightingContext lightingContext )
{
    return wi.z > 0.0f && wo.z > 0.0f ? wi.z * INV_PI : 0.0f;
}

void SampleLambertBRDF( float3 wo
	, float2 sample
    , out float3 wi
    , out float value
    , out float pdf
    , inout LightingContext lightingContext )
{
    wi = ConsineSampleHemisphere( sample );

    LightingContextCalculateH( wo, wi, lightingContext );

    value = wo.z > 0.0f ? INV_PI : 0.0f;
    pdf = wo.z > 0.0f ? wi.z * INV_PI : 0.0f;
}

void SampleLambertBRDF( float3 wo
    , float2 sample
    , out float3 wi
    , inout LightingContext lightingContext )
{
    wi = ConsineSampleHemisphere( sample );

    LightingContextCalculateH( wo, wi, lightingContext );
}

#endif