#ifndef _COOKTORRANCEBRDF_H_
#define _COOKTORRANCEBRDF_H_

#include "Math.inc.hlsl"
#include "LightingContext.inc.hlsl"

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
    return EvaluateGGXMicrofacetDistribution( m, alpha ) * m.z;
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

float EvaluateGGXGeometricShadowingOneDirection( float alpha2, float3 w )
{
    float NdotW = abs( w.z );
    float denominator = sqrt( alpha2 + ( 1.0f - alpha2 ) * NdotW * NdotW ) + NdotW;
    return 2.0f * NdotW / denominator;
}

float EvaluateGGXGeometricShadowing( float3 wi, float3 wo, float alpha )
{
    float alpha2 = alpha * alpha;
    return EvaluateGGXGeometricShadowingOneDirection( alpha2, wi ) * EvaluateGGXGeometricShadowingOneDirection( alpha2, wo );
}

float EvaluateFresnelF0( float ior )
{
    float f0 = ( 1.0f - ior ) / ( 1.0f + ior );
    return f0 * f0;
}

//
// SchlickFresnel
//

float EvaluateSchlickFresnel( float WOdotM, float ior )
{
    float f0 = ( 1.0f - ior ) / ( 1.0f + ior );
    f0 *= f0;
    
    float cosThetaO = WOdotM;
    return ( 1.0f - f0 ) * pow( 1.0f - cosThetaO, 5.0f ) + f0;
}

float EvaluateDielectricFresnel( float WOdotM, float etaI, float etaT )
{
    float cosThetaI = WOdotM;
    float sinThetaI = sqrt( 1.0f - cosThetaI * cosThetaI );
    float sinThetaT = etaI / etaT * sinThetaI;
    if ( sinThetaT >= 1.0f )
    {
        return 1.0f;
    }
    float cosThetaT = sqrt( 1.0f - sinThetaT * sinThetaT );
    float Rparl = ( ( etaT * cosThetaI ) - ( etaI * cosThetaT ) ) /
                  ( ( etaT * cosThetaI ) + ( etaI * cosThetaT ) );
    float Rperp = ( ( etaI * cosThetaI ) - ( etaT * cosThetaT ) ) /
                  ( ( etaI * cosThetaI ) + ( etaT * cosThetaT ) );
    return ( Rparl * Rparl + Rperp * Rperp ) * 0.5f;
}

//
// Cook-Torrance microfacet BRDF
//

float3 EvaluateCookTorranceMircofacetBRDF( float3 wi, float3 wo, float3 reflectance, float alpha, float etaI, float etaT, LightingContext lightingContext )
{
    float WIdotN = lightingContext.WIdotN;
    float WOdotN = lightingContext.WOdotN;
    if ( WIdotN <= 0.0f || WOdotN <= 0.0f )
        return 0.0f;

    float3 m = lightingContext.H;
    if ( all( m == 0.0f ) )
        return 0.0f;

    float WOdotM = dot( wo, m );
    return reflectance * EvaluateGGXMicrofacetDistribution( m, alpha ) * EvaluateGGXGeometricShadowing( wi, wo, alpha ) * EvaluateDielectricFresnel( min( 1.0f, WOdotM ), etaI, etaT ) / ( 4.0f * WIdotN * WOdotN );
}

float EvaluateCookTorranceMicrofacetBRDFPdf( float3 wi, float3 wo, float alpha, LightingContext lightingContext )
{
    float WIdotN = lightingContext.WIdotN;
    float WOdotN = lightingContext.WOdotN;
    if ( WIdotN <= 0.0f || WOdotN <= 0.0f )
        return 0.0f;
    
    float3 m = lightingContext.H;
    float  pdf = EvaluateGGXMicrofacetDistributionPdf( m, alpha );
    return pdf / ( 4.0f * dot( wi, m ) );
}

void SampleCookTorranceMicrofacetBRDF( float3 wo, float2 sample, float3 reflectance, float alpha, float etaI, float etaT, out float3 wi, out float3 value, out float pdf, inout LightingContext lightingContext )
{
    float WOdotN = lightingContext.WOdotN;

    float3 m;
    SampleGGXMicrofacetDistribution( sample, alpha, m );
    wi = -reflect( wo, m );

    lightingContext.H = m;

    float WIdotN = wi.z;
    lightingContext.WIdotN = WIdotN;

    if ( WIdotN <= 0.0f || WOdotN <= 0.0f )
    {
        value = 0.0f;
        pdf = 0.0f;
        return;
    }
    
    value = EvaluateCookTorranceMircofacetBRDF( wi, wo, reflectance, alpha, etaI, etaT, lightingContext );
    pdf   = EvaluateCookTorranceMicrofacetBRDFPdf( wi, wo, alpha, lightingContext );
}

#endif