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

    lightingContext.WIdotN = wi.z;

    // etaT and etaI are swapped becasue we are evaluating fresnel on the opposite hemisphere
    value = lightingContext.WOdotN > 0.0f ? reflectance * ( 1.0f - EvaluateDielectricFresnel( abs( lightingContext.WIdotN ), etaT, etaI ) ) : 0.0f; 
    value *= ( etaI * etaI ) / ( etaT * etaT );
    value /= abs( lightingContext.WIdotN );

    pdf = lightingContext.WOdotN > 0.0f ? 1.0f : 0.0f;
}

#endif
