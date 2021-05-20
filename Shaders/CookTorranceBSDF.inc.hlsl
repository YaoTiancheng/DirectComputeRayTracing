#ifndef _MICROFACET_BSDF_H_
#define _MICROFACET_BSDF_H_

#include "Math.inc.hlsl"
#include "LightingContext.inc.hlsl"
#include "SpecularBRDF.inc.hlsl"
#include "SpecularBTDF.inc.hlsl"

float3 GGXSampleHemisphere( float2 sample, float alpha )
{
    float theta = atan( alpha * sqrt( sample.x / ( 1.0f - sample.x ) ) );
    float phi = 2.0f * PI * sample.y;

    float s = sin( theta );
    return float3( cos( phi ) * s, sin( phi ) * s, cos( theta ) );
}

float EvaluateGGXMicrofacetDistribution( float3 m, float alpha )
{
    float alpha2 = alpha * alpha;
    float NdotM = m.z;
    float NdotM2 = NdotM * NdotM;
    float factor = NdotM2 * ( alpha2 - 1.0f ) + 1.0f;
    float denominator = factor * factor * PI;
    return alpha2 / denominator;
}

float EvaluateGGXMicrofacetDistributionPdf( float3 m, float alpha )
{
    return EvaluateGGXMicrofacetDistribution( m, alpha ) * abs( m.z );
}

void SampleGGXMicrofacetDistribution( float2 sample, float alpha, out float3 m, out float pdf )
{
    m = GGXSampleHemisphere( sample, alpha );
    pdf = EvaluateGGXMicrofacetDistributionPdf( m, alpha );
}

void SampleGGXMicrofacetDistribution( float2 sample, float alpha, out float3 m )
{
    m = GGXSampleHemisphere( sample, alpha );
}

//
// GGX geometric shadowing
//

float EvaluateGGXGeometricShadowingOneDirection( float alpha2, float3 m, float3 w )
{
    // Ensure consistent orientation.
    // (Can't see backfacing microfacet normal from the front and vice versa)
    if ( dot( w, m ) * w.z <= 0.0f )
        return 0.0f;

    float NdotW = abs( w.z );
    float denominator = sqrt( alpha2 + ( 1.0f - alpha2 ) * NdotW * NdotW ) + NdotW;
    return 2.0f * NdotW / denominator;
}

float EvaluateGGXGeometricShadowing( float3 wi, float3 wo, float3 m, float alpha )
{
    float alpha2 = alpha * alpha;
    return EvaluateGGXGeometricShadowingOneDirection( alpha2, m, wi ) * EvaluateGGXGeometricShadowingOneDirection( alpha2, m, wo );
}

#define ALPHA_THRESHOLD 0.000196f

//
// Cook-Torrance microfacet BRDF
//

float3 EvaluateCookTorranceMircofacetBRDF( float3 wi, float3 wo, float3 reflectance, float alpha, float etaI, float etaT, LightingContext lightingContext )
{
    if ( alpha >= ALPHA_THRESHOLD )
    {
        float WIdotN = lightingContext.WIdotN;
        float WOdotN = lightingContext.WOdotN;
        float WOdotM = lightingContext.WOdotH;
        if ( WIdotN <= 0.0f || WOdotN <= 0.0f || WOdotM <= 0.0f )
            return 0.0f;

        float3 m = lightingContext.H;
        if ( all( m == 0.0f ) )
            return 0.0f;

        return reflectance * EvaluateGGXMicrofacetDistribution( m, alpha ) * EvaluateGGXGeometricShadowing( wi, wo, m, alpha ) * EvaluateDielectricFresnel( min( 1.0f, WOdotM ), etaI, etaT ) / ( 4.0f * WIdotN * WOdotN );
    }
    else
    {
        return EvaluateSpecularBRDF( wi, wo );
    }
}

float EvaluateCookTorranceMicrofacetBRDFPdf( float3 wi, float3 wo, float alpha, LightingContext lightingContext )
{
    if ( alpha >= ALPHA_THRESHOLD )
    {
        float WIdotN = lightingContext.WIdotN;
        if ( WIdotN <= 0.0f )
            return 0.0f;

        float3 m = lightingContext.H;
        float WOdotM = lightingContext.WOdotH;
        float pdf = EvaluateGGXMicrofacetDistributionPdf( m, alpha );
        return pdf / ( 4.0f * WOdotM );
    }
    else
    {
        return EvaluateSpecularBRDFPdf( wi, wo );
    }
}

void SampleCookTorranceMicrofacetBRDF( float3 wo, float2 sample, float3 reflectance, float alpha, float etaI, float etaT, out float3 wi, out float3 value, out float pdf, out bool isDeltaBrdf, inout LightingContext lightingContext )
{
    if ( alpha >= ALPHA_THRESHOLD )
    {
        float3 m;
        SampleGGXMicrofacetDistribution( sample, alpha, m );
        wi = -reflect( wo, m );

        LightingContextAssignH( wo, m, lightingContext );

        float WIdotN = wi.z;
        lightingContext.WIdotN = WIdotN;

        value = EvaluateCookTorranceMircofacetBRDF( wi, wo, reflectance, alpha, etaI, etaT, lightingContext );
        pdf = EvaluateCookTorranceMicrofacetBRDFPdf( wi, wo, alpha, lightingContext );
        isDeltaBrdf = false;
    }
    else
    {
        SampleSpecularBRDF( wo, reflectance, etaI, etaT, wi, value, pdf, lightingContext );
        isDeltaBrdf = true;
    }
}

float3 EvaluateCookTorranceMircofacetBTDF( float3 wi, float3 wo, float3 transmittance, float alpha, float etaI, float etaT, LightingContext lightingContext )
{
    if ( alpha >= ALPHA_THRESHOLD )
    {
        float WIdotN = lightingContext.WIdotN;
        float WOdotN = lightingContext.WOdotN;
        if ( WIdotN == 0.0f || WOdotN == 0.0f )
            return 0.0f;

        float3 m = normalize( wo * etaI + wi * etaT );
        if ( m.z < 0.0f ) m = -m; // Ensure same facing as the wo otherwise it will be rejected in G
        float WIdotM = dot( wi, m );
        float WOdotM = dot( wo, m );
        float sqrtDenom = etaI * WOdotM + etaT * WIdotM;

        return ( 1.0f - EvaluateDielectricFresnel( WIdotM, etaI, etaT ) ) * transmittance 
            * abs( EvaluateGGXMicrofacetDistribution( m, alpha ) * EvaluateGGXGeometricShadowing( wi, wo, m, alpha )
            * abs( WIdotM ) * abs( WOdotM ) 
            * etaI * etaI // etaT * etaT * ( ( etaI * etaI ) / ( etaT * etaT ) )
            / ( WOdotN * WIdotN * sqrtDenom * sqrtDenom ) );
    }
    else
    {
        return EvaluateSpecularBTDF( wi, wo );
    }
}

float EvaluateCookTorranceMicrofacetBTDFPdf( float3 wi, float3 wo, float alpha, float etaI, float etaT, LightingContext lightingContext )
{
    if ( alpha >= ALPHA_THRESHOLD )
    {
        float3 m = normalize( wo * etaI + wi * etaT );
        if ( m.z < 0.0f ) m = -m; // Ensure same facing as the wo otherwise it will be rejected in G
        float WIdotM = dot( wi, m );
        float WOdotM = dot( wo, m );
        float sqrtDenom = etaI * WOdotM + etaT * WIdotM;

        float dwh_dwi = abs( ( etaT * etaT * WIdotM ) / ( sqrtDenom * sqrtDenom ) );
        return EvaluateGGXMicrofacetDistributionPdf( m, alpha ) * dwh_dwi;
    }
    else
    {
        return EvaluateSpecularBTDFPdf( wi, wo );
    }
}

void SampleCookTorranceMicrofacetBTDF( float3 wo, float2 sample, float3 transmittance, float alpha, float etaI, float etaT, out float3 wi, out float3 value, out float pdf, out bool isDeltaBtdf, inout LightingContext lightingContext )
{
    value = 0.0f;
    wi    = 0.0f;
    pdf   = 0.0f;

    if ( alpha >= ALPHA_THRESHOLD )
    {
        float WOdotN = lightingContext.WOdotN;
        if ( WOdotN == 0.0f )
            return;

        float3 m;
        SampleGGXMicrofacetDistribution( sample, alpha, m );
        wi = refract( -wo, m, etaI / etaT );
        if ( all( wi == 0.0f ) )
            return;

        LightingContextAssignH( wo, m, lightingContext );

        float WIdotN = wi.z;
        lightingContext.WIdotN = WIdotN;

        value = EvaluateCookTorranceMircofacetBTDF( wi, wo, transmittance, alpha, etaI, etaT, lightingContext );
        pdf   = EvaluateCookTorranceMicrofacetBTDFPdf( wi, wo, alpha, etaI, etaT, lightingContext );

        isDeltaBtdf = false;
    }
    else
    {
        SampleSpecularBTDF( wo, transmittance, etaI, etaT, wi, value, pdf, lightingContext );
        isDeltaBtdf = true;
    }
}

float3 EvaluateCookTorranceMicrofacetBSDF( float3 wi, float3 wo, float3 color, float alpha, float etaI, float etaT, LightingContext lightingContext )
{
    if ( wi.z < 0.0f )
        return EvaluateCookTorranceMircofacetBTDF( wi, wo, color, alpha, etaI, etaT, lightingContext );
    else
        return EvaluateCookTorranceMircofacetBRDF( wi, wo, color, alpha, etaI, etaT, lightingContext );
}

float EvaluateCookTorranceMicrofacetBSDFPdf( float3 wi, float3 wo, float alpha, float etaI, float etaT, LightingContext lightingContext )
{
    if ( wi.z < 0.0f )
        return EvaluateCookTorranceMicrofacetBTDFPdf( wi, wo, alpha, etaI, etaT, lightingContext );
    else
        return EvaluateCookTorranceMicrofacetBRDFPdf( wi, wo, alpha, lightingContext );
}

void SampleCookTorranceMicrofacetBSDF( float3 wo, float selectionSample, float2 bxdfSample, float3 color, float alpha, float etaI, float etaT, out float3 wi, out float3 value, out float pdf, out bool isDeltaBxdf, inout LightingContext lightingContext )
{
    if ( selectionSample < 0.5f )
        SampleCookTorranceMicrofacetBRDF( wo, bxdfSample, color, alpha, etaI, etaT, wi, value, pdf, isDeltaBxdf, lightingContext );
    else
        SampleCookTorranceMicrofacetBTDF( wo, bxdfSample, color, alpha, etaI, etaT, wi, value, pdf, isDeltaBxdf, lightingContext );

    pdf *= 0.5f;
}

#endif