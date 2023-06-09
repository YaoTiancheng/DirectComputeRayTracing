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
    lightingContext.H = float3( 0.0f, 0.0f, 1.0f );
    lightingContext.WOdotH = wo.z;

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

float EvaluateSpecularBSDF( float3 wi, float3 wo )
{
    return 0.0f;
}

float EvaluateSpecularBSDFPdf( float3 wi, float3 wo )
{
    return 0.0f;
}

void SampleSpecularBSDF( float3 wo, float sample, float etaI, float etaT, out float3 wi, inout float value, inout float pdf, inout LightingContext lightingContext )
{
    wi = 0.0f;

    lightingContext.H = float3( 0.0f, 0.0f, 1.0f );
    lightingContext.WOdotH = wo.z;

    if ( etaI == etaT )
    {
        value = 1.0f / wo.z;
        pdf = 1.0f;
        wi = -wo;
        return;
    }

    if ( wo.z == 0.0f )
        return;

    float F = FresnelDielectric( wo.z, etaI, etaT );
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
        wi = refract( -wo, float3( 0.0f, 0.0f, 1.0f ), etaI / etaT );

        if ( wi.z == 0.0f )
            return;

        value = ( 1.0f - FresnelDielectric( wi.z, etaI, etaT ) )
              * ( etaI * etaI ) / ( etaT * etaT ) 
              / ( -wi.z );

        pdf = 1.0f - F;
    }
}

#endif