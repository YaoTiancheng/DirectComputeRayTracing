#ifndef _SPECULAR_BRDF_H_
#define _SPECULAR_BRDF_H_

#include "LightingContext.inc.hlsl"
#include "Fresnel.inc.hlsl"

float3 EvaluateSpecularBRDF( float3 wi, float3 wo )
{
    return 0.0f;
}

float EvaluateSpecularBRDFPdf( float3 wi, float3 wo )
{
    return 0.0f;
}

void SampleSpecularBRDF( float3 wo, float3 reflectance, float etaI, float etaT, out float3 wi, out float3 value, out float pdf, inout LightingContext lightingContext )
{
    value = 0.0f;
    pdf   = 0.0f;

    wi = float3( -wo.x, -wo.y, wo.z );

    if ( lightingContext.WOdotN == 0.0f )
        return;

    value = reflectance * EvaluateDielectricFresnel( lightingContext.WOdotN, etaI, etaT ) / wi.z;
    pdf   = 1.0f;

    lightingContext.WIdotN = wi.z;
    lightingContext.H = float3( 0.0f, 0.0f, 1.0f );
    lightingContext.WOdotH = lightingContext.WOdotN;
}

#endif