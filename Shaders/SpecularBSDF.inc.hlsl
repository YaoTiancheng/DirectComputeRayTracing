#ifndef _SPECULAR_BSDF_H_
#define _SPECULAR_BSDF_H_

#include "SpecularBRDF.inc.hlsl"
#include "SpecularBTDF.inc.hlsl"

float3 EvaluateSpecularBSDF( float3 wi, float3 wo )
{
    return 0.0f;
}

float EvaluateSpecularBSDFPdf( float3 wi, float3 wo )
{
    return 0.0f;
}

void SampleSpecularBSDF( float3 wo, float sample, float3 reflectance, float etaI, float etaT, out float3 wi, out float3 value, out float pdf, inout LightingContext lightingContext )
{
    if ( sample < 0.5f )
        SampleSpecularBRDF( wo, reflectance, etaI, etaT, wi, value, pdf, lightingContext );
    else
        SampleSpecularBTDF( wo, reflectance, etaI, etaT, wi, value, pdf, lightingContext );

    pdf *= 0.5f;
}

#endif