#ifndef _SPECULAR_BXDF_H_
#define _SPECULAR_BXDF_H_

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

float EvaluateSpecularBSDF( float3 wi, float3 wo )
{
    return 0.0f;
}

float EvaluateSpecularBSDFPdf( float3 wi, float3 wo )
{
    return 0.0f;
}

void SampleSpecularBSDF( float3 wo, float sample, float etaO, float etaI, out float3 wi, inout float value, inout float pdf, inout LightingContext lightingContext )
{
    wi = 0.0f;

    lightingContext.H = float3( 0.0f, 0.0f, 1.0f );
    lightingContext.WOdotH = wo.z;

    if ( etaO == etaI )
    {
        value = 1.0f / wo.z;
        pdf = 1.0f;
        wi = -wo;
        return;
    }

    if ( wo.z == 0.0f )
        return;

    float F = FresnelDielectric( wo.z, etaO, etaI );
    bool sampleReflection = sample < F;
    if ( sampleReflection )
    {
        wi = wo;
        wi.xy = -wi.xy;
        
        value = F / wi.z;
        pdf = F;
    }
    else
    {
        wi = refract( -wo, float3( 0.0f, 0.0f, 1.0f ), etaO / etaI );

        if ( wi.z == 0.0f )
            return;

        value = ( 1.0f - F )
#if !defined( REFRACTION_NO_SCALE_FACTOR )
              * ( etaO * etaO ) / ( etaI * etaI ) 
#endif
              / ( -wi.z );

        pdf = 1.0f - F;
    }
}

#endif