#ifndef _SPECULAR_BTDF_H_
#define _SPECULAR_BTDF_H_

#include "LightingContext.inc.hlsl"
#include "Fresnel.inc.hlsl"

float EvaluateSpecularBTDF( float3 wi, float3 wo )
{
    return 0.0f;
}

float EvaluateSpecularBTDFPdf( float3 wi, float3 wo )
{
    return 0.0f;
}

void SampleSpecularBTDF( float3 wo, float etaI, float etaT, out float3 wi, inout float value, inout float pdf, inout LightingContext lightingContext )
{
    wi = refract( -wo, float3( 0.0f, 0.0f, 1.0f ), etaI / etaT );
    if ( all( wi == 0.0f ) )
        return;

    if ( wi.z == 0.0f || wo.z == 0.0f )
        return;

    value = ( 1.0f - FresnelDielectric( wi.z, etaI, etaT ) )
        * ( etaI * etaI ) / ( etaT * etaT ) 
        / abs( wi.z );

    pdf = 1.0f;
}

#endif
