#ifndef _KULLACONTY_H_
#define _KULLACONTY_H_

#include "Math.inc.hlsl"
#include "LightingContext.inc.hlsl"
#include "MonteCarlo.inc.hlsl"
#include "BxDFTextures.inc.hlsl"

//
// Multiscattering BxDF
//

float MultiscatteringFavgDielectric( float eta )
{
    float eta2 = eta * eta;
    return eta >= 1.0f 
        ? ( eta - 1.0f ) / ( 4.08567f + 1.00071f * eta ) 
        : 0.997118f + 0.1014f * eta - 0.965241f * eta2 - 0.130607f * eta2 * eta;
}

float3 MultiscatteringFavgConductor( float3 eta, float3 k )
{
    // Full cubic 3D polynomial fitting
    /*bool3 smallEta = eta < 1.0f;
    float3 a = smallEta ?  9.4555597433543637E-01 :  1.0246918453560364E-01;
    float3 b = smallEta ? -1.2553345230422654E+00 : -2.8053691279081833E-02;
    float3 c = smallEta ?  5.0376101078453521E-02 :  2.3334989012529267E-01;
    float3 d = smallEta ?  3.6819267468471062E-01 :  2.5793790574058471E-02;
    float3 f = smallEta ? -1.0930166711608846E-02 : -1.1253806018681275E-02;
    float3 g = smallEta ? -2.8365267538531167E-02 : -1.7015953550076728E-03;
    float3 h = smallEta ?  6.6875946203492909E-04 : -2.5759179444232935E-04;
    float3 i = smallEta ?  2.8032399654992435E-01 : -3.8746959437472639E-02;
    float3 j = smallEta ? -4.4320434429047734E-02 :  3.0787666065079829E-04;
    float3 k_ = smallEta ? -1.6063213773073390E-02 :  2.6796891787734967E-03;
    float3 eta2 = eta * eta;
    float3 eta3 = eta2 * eta;
    float3 k2 = k * k;
    float3 k3 = k2 * k;
    float3 temp = 0.f;
    temp = a;
    temp += b * eta;
    temp += c * k;
    temp += d * eta2;
    temp += f * k2;
    temp += g * eta3;
    temp += h * k3;
    temp += i * eta * k;
    temp += j * eta2 * k;
    temp += k_ * eta * k2;
    return saturate( temp );*/

    // Approximation for the hemispherical albedo of a smooth conductor ( Hitchikers Guide to Multiple Scattering ), Eq.(12.9)
    float3 numerator   = eta * ( 133.736f - 98.9833f * eta ) + k * ( eta * ( 59.5617f - 3.98288f * eta ) - 182.37f ) + ( ( 0.30818f * eta - 13.1093f ) * eta - 62.5919f ) * k * k - 8.21474f;
    float3 denominator = k * ( eta * ( 94.6517f - 15.8558f * eta ) - 187.166f ) + ( -78.476 * eta - 395.268f ) * eta + ( eta * ( eta - 15.4387f ) - 62.0752f ) * k * k;
    return saturate( numerator / denominator );
}

float MultiscatteringFresnel( float Eavg, float Favg )
{
    return Favg * Favg * Eavg / ( 1.0f - Favg * ( 1.0f - Eavg ) );
}

float3 MultiscatteringFresnel( float Eavg, float3 Favg )
{
    return Favg * Favg * Eavg / ( 1.0f - Favg * ( 1.0f - Eavg ) );
}

float MultiscatteringBxDF( float Ei, float Eo, float Eavg )
{
    return Eavg < 1.0f
        ? ( 1.0f - Ei ) * ( 1.0f - Eo ) / ( PI * ( 1.0f - Eavg ) )
        : 0.0f;
}

//
// Multiscattering BSDF
//

float EvaluateCookTorranceMultiscatteringBSDF( float3 wi, float alpha, float ratio, float eta, float invEta, float Eo, float Eavg, float Eavg_inv )
{
    float cosThetaI = abs( wi.z );
    if ( cosThetaI == 0.0f )
        return 0.0f;

    bool evaluateReflection = wi.z > 0.0f;
    float Ei = SampleBSDFTexture( cosThetaI, alpha, evaluateReflection ? eta : invEta );
    float factor = evaluateReflection ? ( 1.0f - ratio ) : ratio;
    return MultiscatteringBxDF( Ei, Eo, evaluateReflection ? Eavg : Eavg_inv ) * factor;
}

float EvaluateCookTorranceMultiscatteringBSDFPdf( float3 wi, float ratio )
{
    float cosThetaI = abs( wi.z );
    if ( cosThetaI == 0.0f )
        return 0.0f;

    bool sampleReflection = wi.z > 0.0f;
    float pdf = abs( wi.z ) * INV_PI;
    pdf *= sampleReflection ? 1.0f - ratio : ratio;
    return pdf;
}

void SampleCookTorranceMultiscatteringBSDF( float3 wo, float selectionSample, float2 bxdfSample, float ratio, out float3 wi, LightingContext lightingContext )
{
    wi = 0.0f;

    if ( wo.z == 0.0f )
        return;

    bool sampleReflection = selectionSample >= ratio;

    wi = ConsineSampleHemisphere( bxdfSample );

    if ( !sampleReflection )
        wi.z = -wi.z;

    LightingContextCalculateH( wo, wi, lightingContext );
}

float ReciprocalFactor( float Favg, float Favg_inv, float Eavg, float Eavg_inv, float invEta )
{
    float numerator = ( 1.0f - Favg_inv ) * ( 1.0f - Eavg_inv );
    float denominator = ( 1.0f - Favg ) * ( 1.0f - Eavg );
    float factor = numerator * invEta * invEta / max( 0.00001f, denominator );
    float x = factor / ( 1.0f + factor );
    return x;
}

//
// Multiscattering BRDF
//

float3 EvaluateCookTorranceMultiscatteringBRDF( float3 wi, float3 wo, float alpha, float Eo, float Eavg, float3 factor, LightingContext lightingContext )
{
    float cosThetaO = wo.z;
    float cosThetaI = wi.z;
    if ( cosThetaO <= 0.0f || cosThetaI <= 0.0f )
        return 0.0f;

    float Ei = SampleBRDFTexture( cosThetaI, alpha );
    return MultiscatteringBxDF( Ei, Eo, Eavg ) * factor;
}

float EvaluateCookTorranceMultiscatteringBRDFPdf( float3 wi, float3 wo, LightingContext lightingContext )
{
    float cosThetaO = wo.z;
    float cosThetaI = wi.z;
    if ( cosThetaO <= 0.0f || cosThetaI <= 0.0f )
        return 0.0f;

    return wi.z * INV_PI;
}

void SampleCookTorranceMultiscatteringBRDF( float3 wo, float2 bxdfSample, out float3 wi, inout LightingContext lightingContext )
{
    wi = ConsineSampleHemisphere( bxdfSample );

    LightingContextCalculateH( wo, wi, lightingContext );
}

#endif