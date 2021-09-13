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

void SampleSpecularBRDF( float3 wo, float3 reflectance, float3 F, out float3 wi, out float3 value, out float pdf, inout LightingContext lightingContext )
{
    value = 0.0f;
    pdf   = 0.0f;

    wi = float3( -wo.x, -wo.y, wo.z );

    if ( wo.z == 0.0f || lightingContext.isInverted )
        return;

    value = reflectance * F / wi.z;
    pdf   = 1.0f;

    lightingContext.H = float3( 0.0f, 0.0f, 1.0f );
    lightingContext.WOdotH = wo.z;
}

void SampleSpecularBRDF_Dielectric( float3 wo, float3 reflectance, float etaI, float etaT, out float3 wi, out float3 value, out float pdf, inout LightingContext lightingContext )
{
    float F = FresnelDielectric( wo.z, etaI, etaT );
    SampleSpecularBRDF( wo, reflectance, F, wi, value, pdf, lightingContext );
}

void SampleSpecularBRDF_Conductor( float3 wo, float3 reflectance, float3 etaI, float3 etaT, float3 k, out float3 wi, out float3 value, out float pdf, inout LightingContext lightingContext )
{
    float3 F = FresnelConductor( wo.z, etaI, etaT, k );
    SampleSpecularBRDF( wo, reflectance, F, wi, value, pdf, lightingContext );
}

void SampleSpecularBRDF_Schlick( float3 wo, float3 reflectance, float3 F0, out float3 wi, out float3 value, out float pdf, inout LightingContext lightingContext )
{
    float3 F = FresnelSchlick( wo.z, F0 );
    SampleSpecularBRDF( wo, reflectance, F, wi, value, pdf, lightingContext );
}

#endif