#ifndef _MICROFACET_BSDF_H_
#define _MICROFACET_BSDF_H_

#include "Math.inc.hlsl"
#include "LightingContext.inc.hlsl"
#include "BxDFTextures.inc.hlsl"
#include "MonteCarlo.inc.hlsl"

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

//
// NDF
//

float3 SampleGGXNDF( float2 sample, float alpha )
{
    float theta = atan( alpha * sqrt( sample.x / ( 1.0f - sample.x ) ) );
    float phi = 2.0f * PI * sample.y;

    float s = sin( theta );
    return float3( cos( phi ) * s, sin( phi ) * s, cos( theta ) );
}

// From "Sampling the GGX Distribution of Visible Normals" by Eric Heitz
float3 SampleGGXVNDF( float3 wo, float2 sample, float alpha )
{
    float U1 = sample.x;
    float U2 = sample.y;
    // Section 3.2: transforming the view direction to the hemisphere configuration
    float3 Vh = normalize( float3( alpha * wo.x, alpha * wo.y, wo.z ) );
    // Section 4.1: orthonormal basis (with special case if cross product is zero)
    float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
    float3 T1 = lensq > 0 ? float3( -Vh.y, Vh.x, 0 ) / sqrt( lensq ) : float3( 1, 0, 0 );
    float3 T2 = cross( Vh, T1 );
    // Section 4.2: parameterization of the projected area
    float r = sqrt( U1 );
    float phi = 2.0 * PI * U2;
    float t1 = r * cos( phi );
    float t2 = r * sin( phi );
    float s = 0.5 * ( 1.0 + Vh.z );
    t2 = ( 1.0 - s ) * sqrt( 1.0 - t1 * t1 ) + s * t2;
    // Section 4.3: reprojection onto hemisphere
    float3 Nh = t1 * T1 + t2 * T2 + sqrt( max( 0.0, 1.0 - t1 * t1 - t2 * t2 ) ) * Vh;
    // Section 3.4: transforming the normal back to the ellipsoid configuration
    float3 Ne = normalize( float3( alpha * Nh.x, alpha * Nh.y, max( 0.0, Nh.z ) ) );
    return Ne;
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

float EvaluateGGXMicrofacetDistributionPdf( float3 wo, float3 m, float alpha )
{
#if defined( GGX_SAMPLE_VNDF )
    return EvaluateGGXMicrofacetDistribution( m, alpha ) * EvaluateGGXGeometricShadowingOneDirection( alpha * alpha, m, wo ) * max( 0, dot( wo, m ) ) / wo.z;
#else
    return EvaluateGGXMicrofacetDistribution( m, alpha ) * abs( m.z );
#endif
}

void SampleGGXMicrofacetDistribution( float3 wo, float2 sample, float alpha, out float3 m, out float pdf )
{
#if defined( GGX_SAMPLE_VNDF )
    m = SampleGGXVNDF( wo, sample, alpha );
#else 
    m = SampleGGXNDF( sample, alpha );
#endif
    pdf = EvaluateGGXMicrofacetDistributionPdf( wo, m, alpha );
}

void SampleGGXMicrofacetDistribution( float3 wo, float2 sample, float alpha, out float3 m )
{
#if defined( GGX_SAMPLE_VNDF )
    m = SampleGGXVNDF( wo, sample, alpha );
#else
    m = SampleGGXNDF( sample, alpha );
#endif
}

//
// Cook-Torrance microfacet BRDF
//

float EvaluateCookTorranceMircofacetBRDF( float3 wi, float3 wo, float alpha, LightingContext lightingContext )
{
    float WIdotN = wi.z;
    float WOdotN = wo.z;
    float WOdotM = lightingContext.WOdotH;
    if ( WIdotN <= 0.0f || WOdotN <= 0.0f || WOdotM <= 0.0f )
        return 0.0f;

    float3 m = lightingContext.H;
    if ( all( m == 0.0f ) )
        return 0.0f;

    return EvaluateGGXMicrofacetDistribution( m, alpha ) * EvaluateGGXGeometricShadowing( wi, wo, m, alpha ) / ( 4.0f * WIdotN * WOdotN );
}

float EvaluateCookTorranceMicrofacetBRDFPdf( float3 wi, float3 wo, float alpha, LightingContext lightingContext )
{
    float WIdotN = wi.z;
    float WOdotN = wo.z;
    float WOdotM = lightingContext.WOdotH;
    if ( WIdotN <= 0.0f || WOdotN <= 0.0f || WOdotM <= 0.0f )
        return 0.0f;

    float3 m = lightingContext.H;
    float pdf = EvaluateGGXMicrofacetDistributionPdf( wo, m, alpha );
    return pdf / ( 4.0f * WOdotM );
}

void SampleCookTorranceMicrofacetBRDF( float3 wo, float2 sample, float alpha, out float3 wi, inout LightingContext lightingContext )
{
    float3 m;
    SampleGGXMicrofacetDistribution( wo, sample, alpha, m );
    wi = -reflect( wo, m );

    LightingContextAssignH( wo, m, lightingContext );
}

//
// Microfacet BSDF
//

float EvaluateCookTorranceMicrofacetBSDF( float3 wi, float3 wo, float alpha, float etaI, float etaT, LightingContext lightingContext )
{
    bool active = wo.z != 0.0f && wi.z != 0.0f;

    bool reflect = wi.z * wo.z > 0.0f;

    float3 m = normalize( wo * ( reflect ? 1.0f : etaI ) + wi * ( reflect ? 1.0f : etaT ) );

    m = m.z < 0.0f ? -m : m;

    float WIdotM = dot( wi, m );
    float WOdotM = dot( wo, m );

    float D = EvaluateGGXMicrofacetDistribution( m, alpha );
    float F = FresnelDielectric( WOdotM, etaI, etaT );
    float G = EvaluateGGXGeometricShadowing( wi, wo, m, alpha );

    float WIdotN = wi.z;
    float WOdotN = wo.z;

    if ( reflect )
    {
        return active ? F * D * G / ( 4.0f * abs( WIdotN ) * abs( WOdotN ) ) : 0.0f;
    }
    else
    {
        float sqrtDenom = etaI * WOdotM + etaT * WIdotM;
        float value = ( 1.0f - F ) * abs( D * G
                      * abs( WIdotM ) * abs( WOdotM ) 
                      * etaI * etaI // etaT * etaT * ( ( etaI * etaI ) / ( etaT * etaT ) )
                      / ( WOdotN * WIdotN * sqrtDenom * sqrtDenom ) );
        return active ? value : 0.0f;
    }
}

float EvaluateCookTorranceMicrofacetBSDFPdf( float3 wi, float3 wo, float alpha, float etaI, float etaT, LightingContext lightingContext )
{
    bool active = wo.z != 0.0f && wi.z != 0.0f;

    bool reflect = wi.z * wo.z > 0.0f;

    float3 m = normalize( wo * ( reflect ? 1.0f : etaI ) + wi * ( reflect ? 1.0f : etaT ) );

    m = m.z < 0.0f ? -m : m;

    float WIdotM = dot( wi, m );
    float WOdotM = dot( wo, m );

    // Ensure consistent orientation.
    // (Can't see backfacing microfacet normal from the front and vice versa)
    active = active && ( WIdotM * wi.z > 0.0f && WOdotM * wo.z > 0.0f );

    float sqrtDenom = etaI * WOdotM + etaT * WIdotM;
    float dwh_dwi = reflect ? 1.0f / ( 4.0f * WIdotM ) : abs( ( etaT * etaT * WIdotM ) / ( sqrtDenom * sqrtDenom ) );

    float pdf = EvaluateGGXMicrofacetDistributionPdf( wo, m, alpha );

    float F = FresnelDielectric( WOdotM, etaI, etaT );

    return active ? pdf * ( reflect ? F : 1.0f - F ) * dwh_dwi : 0.0f;
}

void SampleCookTorranceMicrofacetBSDF( float3 wo, float selectionSample, float2 bxdfSample, float alpha, float etaI, float etaT, out float3 wi, inout LightingContext lightingContext )
{
    wi = 0.0f;

    if ( wo.z == 0.0f )
    {
        return;
    }

    if ( etaI == etaT )
    {
        wi = -wo;
        return;
    }

    float3 m;
    SampleGGXMicrofacetDistribution( wo, bxdfSample, alpha, m );
    
    float WOdotM = dot( wo, m );

    lightingContext.H = m;
    lightingContext.WOdotH = WOdotM;

    if ( WOdotM <= 0.0f )
    {
        return;
    }

    float F = FresnelDielectric( WOdotM, etaI, etaT );
    bool sampleReflection = selectionSample < F;
    if ( sampleReflection )
    {
        wi = -reflect( wo, m );
    }
    else
    {
        wi = refract( -wo, m, etaI / etaT );
    }
}

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

float3 CookTorranceMultiscatteringBSDFSampleHemisphere( float2 sample, float alpha, float eta )
{
    float cosThetaI = SampleCookTorranceMicrofacetBSDFInvCDFTexture( sample.x, alpha, eta );
    float phi = 2.0f * PI * sample.y;
    float s = sqrt( 1 - cosThetaI * cosThetaI );
    return float3( cos( phi ) * s, sin( phi ) * s, cosThetaI );
}

float EvaluateCookTorranceMultiscatteringBSDF( float3 wi, float alpha, float ratio, float eta, float invEta, float Eo, float Eavg, float Eavg_inv )
{
    float cosThetaI = abs( wi.z );
    if ( cosThetaI == 0.0f )
        return 0.0f;

    bool evaluateReflection = wi.z > 0.0f;
    float Ei = SampleCookTorranceMicrofacetBSDFEnergyTexture( cosThetaI, alpha, evaluateReflection ? eta : invEta );
    float factor = evaluateReflection ? ( 1.0f - ratio ) : ratio;
    return MultiscatteringBxDF( Ei, Eo, evaluateReflection ? Eavg : Eavg_inv ) * factor;
}

float EvaluateCookTorranceMultiscatteringBSDFPdf( float3 wi, float alpha, float ratio, float eta, float invEta )
{
    float cosThetaI = abs( wi.z );
    if ( cosThetaI == 0.0f )
        return 0.0f;

    bool sampleReflection = wi.z > 0.0f;
    float pdfScale = SampleCookTorranceMicrofacetBSDFPDFScaleTexture( alpha, sampleReflection ? eta : invEta );
    float Ei = SampleCookTorranceMicrofacetBSDFEnergyTexture( cosThetaI, alpha, sampleReflection ? eta : invEta );
    float pdf = pdfScale > 0.0f 
        ? ( 1.0f - Ei ) * cosThetaI / pdfScale 
        : 0.0f;
    pdf *= sampleReflection ? 1.0f - ratio : ratio;
    return pdf;
}

void SampleCookTorranceMultiscatteringBSDF( float3 wo, float selectionSample, float2 bxdfSample, float alpha, float ratio, float eta, float invEta, out float3 wi, LightingContext lightingContext )
{
    wi = 0.0f;

    if ( wo.z == 0.0f )
        return;

    bool sampleReflection = selectionSample >= ratio;

    wi = CookTorranceMultiscatteringBSDFSampleHemisphere( bxdfSample, alpha, sampleReflection ? eta : invEta );

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

float3 CookTorranceMultiscatteringBRDFSampleHemisphere( float2 sample, float alpha )
{
    float cosThetaI = SampleCookTorranceMicrofacetBRDFInvCDFTexture( sample.x, alpha );
    float phi = 2.0f * PI * sample.y;
    float s = sqrt( 1 - cosThetaI * cosThetaI );
    return float3( cos( phi ) * s, sin( phi ) * s, cosThetaI );
}

float3 EvaluateCookTorranceMultiscatteringBRDF( float3 wi, float3 wo, float alpha, float Eo, float Eavg, float3 factor, LightingContext lightingContext )
{
    float cosThetaO = wo.z;
    float cosThetaI = wi.z;
    if ( cosThetaO <= 0.0f || cosThetaI <= 0.0f )
        return 0.0f;

    float Ei = SampleCookTorranceMicrofacetBRDFEnergyTexture( cosThetaI, alpha );
    return MultiscatteringBxDF( Ei, Eo, Eavg ) * factor;
}

float EvaluateCookTorranceMultiscatteringBRDFPdf( float3 wi, float3 wo, float alpha, LightingContext lightingContext )
{
    float cosThetaO = wo.z;
    float cosThetaI = wi.z;
    if ( cosThetaO <= 0.0f || cosThetaI <= 0.0f )
        return 0.0f;

    float pdfScale = SampleCookTorranceMicrofacetBRDFPDFScaleTexture( alpha );
    float Ei = SampleCookTorranceMicrofacetBRDFEnergyTexture( cosThetaI, alpha );
    return pdfScale > 0.0f
        ? ( 1.0f - Ei ) * cosThetaI / pdfScale
        : 0.0f;
}

void SampleCookTorranceMultiscatteringBRDF( float3 wo, float2 bxdfSample, float alpha, float Eo, float Eavg, float3 Fms, out float3 wi, inout float3 value, inout float pdf, inout LightingContext lightingContext )
{
    wi = CookTorranceMultiscatteringBRDFSampleHemisphere( bxdfSample, alpha );

    LightingContextCalculateH( wo, wi, lightingContext );

    float cosThetaO = wo.z;
    float cosThetaI = wi.z;
    if ( cosThetaO <= 0.0f || cosThetaI <= 0.0f )
        return;

    float Ei = SampleCookTorranceMicrofacetBRDFEnergyTexture( cosThetaI, alpha );
    value = MultiscatteringBxDF( Ei, Eo, Eavg ) * Fms;

    float pdfScale = SampleCookTorranceMicrofacetBRDFPDFScaleTexture( alpha );
    pdf = pdfScale > 0.0f
        ? ( 1.0f - Ei ) * cosThetaI / pdfScale
        : 0.0f;
}

void SampleCookTorranceMultiscatteringBRDF( float3 wo, float2 bxdfSample, float alpha, out float3 wi, inout LightingContext lightingContext )
{
    wi = CookTorranceMultiscatteringBRDFSampleHemisphere( bxdfSample, alpha );

    LightingContextCalculateH( wo, wi, lightingContext );
}

#endif