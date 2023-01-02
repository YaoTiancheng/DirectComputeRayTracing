#ifndef _SPECULAR_BRDF_H_
#define _SPECULAR_BRDF_H_

#include "LightingContext.inc.hlsl"
#include "Fresnel.inc.hlsl"

float EvaluateSpecularBRDF( float3 wi, float3 wo )
{
    return 0.0f;
}

float EvaluateSpecularBRDFPdf( float3 wi, float3 wo )
{
    return 0.0f;
}

void SampleSpecularBRDF( float3 wo, out float3 wi, inout float value, inout float pdf, inout LightingContext lightingContext )
{
    wi = float3( -wo.x, -wo.y, wo.z );

    lightingContext.H = float3( 0.0f, 0.0f, 1.0f );
    lightingContext.WOdotH = wo.z;

    if ( wo.z <= 0.0f )
        return;

    value = 1.0f / wi.z;
    pdf = 1.0f;
}

void SampleSpecularBRDF_Dielectric( float3 wo, float etaI, float etaT, out float3 wi, inout float value, inout float pdf, inout LightingContext lightingContext )
{
    float F = FresnelDielectric( wo.z, etaI, etaT );
    SampleSpecularBRDF( wo, wi, value, pdf, lightingContext );
    value *= F;
}

void SampleSpecularBRDF_Conductor( float3 wo, float3 etaI, float3 etaT, float3 k, out float3 wi, inout float3 value, inout float pdf, inout LightingContext lightingContext )
{
    float3 F = FresnelConductor( wo.z, etaI, etaT, k );
    float factor = 0.0f;
    SampleSpecularBRDF( wo, wi, factor, pdf, lightingContext );
    value = factor * F;
}

void SampleSpecularBRDF_Schlick( float3 wo, float3 F0, out float3 wi, inout float3 value, inout float pdf, inout LightingContext lightingContext )
{
    float3 F = FresnelSchlick( wo.z, F0 );
    float factor = 0.0f;
    SampleSpecularBRDF( wo, wi, factor, pdf, lightingContext );
    value = factor * F;
}

#endif