#ifndef _SPECULAR_BTDF_H_
#define _SPECULAR_BTDF_H_

#include "LightingContext.inc.hlsl"
#include "Fresnel.inc.hlsl"

float3 EvaluateSpecularBTDF( float3 wi, float3 wo )
{
    return 0.0f;
}

float EvaluateSpecularBTDFPdf( float3 wi, float3 wo )
{
    return 0.0f;
}

void SampleSpecularBTDF( float3 wo, float3 reflectance, float etaI, float etaT, out float3 wi, out float3 value, out float pdf, inout LightingContext lightingContext )
{
    value = 0.0f;
    pdf = 0.0f;

    wi = refract( -wo, float3( 0.0f, 0.0f, 1.0f ), etaI / etaT );
    if ( all( wi == 0.0f ) )
        return;

    if ( wi.z == 0.0f || wo.z == 0.0f )
        return;

    value = reflectance * ( 1.0f - FresnelDielectric( wi.z, etaI, etaT ) )
        * ( etaI * etaI ) / ( etaT * etaT ) 
        / abs( wi.z );

    pdf = 1.0f;
}

#endif
