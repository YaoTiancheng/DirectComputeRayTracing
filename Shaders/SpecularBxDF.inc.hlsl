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

void SampleSpecularBSDF( float3 wo, float sample, float etaO, float etaI, bool isThin, out float3 wi, inout float value, inout float pdf, inout LightingContext lightingContext )
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
    float T = 1.f - F;
    if ( isThin && F < 1.f )
    {
        F += T * T * F / ( 1.f - F * F );
        T = 1.f - F;
    }

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
        if ( !isThin )
        {
            wi = refract( -wo, float3( 0.0f, 0.0f, 1.0f ), etaO / etaI );
        }
        else
        {
            wi = -wo;
        }

        if ( wi.z == 0.0f )
            return;
        
        value = T
#if !defined( REFRACTION_NO_SCALE_FACTOR )
              * ( !isThin ? ( etaO * etaO ) / ( etaI * etaI ) : 1.f )
#endif
              / ( -wi.z );

        pdf = T;
    }
}

#endif