#ifndef _MICROFACET_BSDF_H_
#define _MICROFACET_BSDF_H_

#include "Math.inc.hlsl"
#include "LightingContext.inc.hlsl"
#include "SpecularBRDF.inc.hlsl"
#include "SpecularBTDF.inc.hlsl"
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
    return EvaluateGGXMicrofacetDistribution( m, alpha ) * EvaluateGGXGeometricShadowingOneDirection( alpha * alpha, m, wo ) * max( 0, dot( wo, m ) ) / wo.z;
}

void SampleGGXMicrofacetDistribution( float3 wo, float2 sample, float alpha, out float3 m, out float pdf )
{
    m = SampleGGXVNDF( wo, sample, alpha );
    pdf = EvaluateGGXMicrofacetDistributionPdf( wo, m, alpha );
}

void SampleGGXMicrofacetDistribution( float3 wo, float2 sample, float alpha, out float3 m )
{
    m = SampleGGXVNDF( wo, sample, alpha );
}

#define ALPHA_THRESHOLD 0.00052441f

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
        float pdf = EvaluateGGXMicrofacetDistributionPdf( wo, m, alpha );
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
        SampleGGXMicrofacetDistribution( wo, sample, alpha, m );
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

//
// Microfacet BTDF
//

float CookTorranceMicrofacetBTDF( float3 wi, float3 wo, float3 m, float alpha, float etaI, float etaT, float WIdotN, float WOdotN, float WIdotM, float WOdotM, float sqrtDenom )
{
    return sqrtDenom != 0 ? 
        ( 1.0f - EvaluateDielectricFresnel( WIdotM, etaI, etaT ) ) * abs( EvaluateGGXMicrofacetDistribution( m, alpha ) * EvaluateGGXGeometricShadowing( wi, wo, m, alpha )
        * abs( WIdotM ) * abs( WOdotM ) 
        * etaI * etaI // etaT * etaT * ( ( etaI * etaI ) / ( etaT * etaT ) )
        / ( WOdotN * WIdotN * sqrtDenom * sqrtDenom ) )
        :
        1.0f;
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

        return CookTorranceMicrofacetBTDF( wi, wo, m, alpha, etaI, etaT, WIdotN, WOdotN, WIdotM, WOdotM, sqrtDenom ) * transmittance;
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
        return EvaluateGGXMicrofacetDistributionPdf( wo, m, alpha ) * dwh_dwi;
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
        SampleGGXMicrofacetDistribution( wo, sample, alpha, m );
        wi = refract( -wo, m, etaI / etaT );
        if ( all( wi == 0.0f ) )
            return;

        LightingContextAssignH( wo, m, lightingContext );

        float WIdotN = wi.z;
        lightingContext.WIdotN = WIdotN;

        if ( WIdotN == 0.0f )
            return;

        float WIdotM = dot( wi, m );
        float WOdotM = lightingContext.WOdotH;
        float sqrtDenom = etaI * WOdotM + etaT * WIdotM;

        value = CookTorranceMicrofacetBTDF( wi, wo, m, alpha, etaI, etaT, WIdotN, WOdotN, WIdotM, WOdotM, sqrtDenom ) * transmittance;

        float dwh_dwi = abs( ( etaT * etaT * WIdotM ) / ( sqrtDenom * sqrtDenom ) );
        pdf = sqrtDenom != 0.0f ? EvaluateGGXMicrofacetDistributionPdf( wo, m, alpha ) * dwh_dwi : 1.0f;

        isDeltaBtdf = false;
    }
    else
    {
        SampleSpecularBTDF( wo, transmittance, etaI, etaT, wi, value, pdf, lightingContext );
        isDeltaBtdf = true;
    }
}

//
// Microfacet BSDF
//

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
    float cosThetaO = wo.z;
    float eta = etaT / etaI;
    float invEta = etaI / etaT;
    float Efresnel_r = SampleCookTorranceMicrofacetBRDFEnergyFresnelDielectricTexture( cosThetaO, alpha, eta );
    float Efresnel_t = SampleCookTorranceMicrofacetBTDFEnergyTexture( cosThetaO, alpha, eta ) * invEta * invEta;
    float Wr = Efresnel_r / ( Efresnel_r + Efresnel_t );

    if ( selectionSample < Wr )
    {
        SampleCookTorranceMicrofacetBRDF( wo, bxdfSample, color, alpha, etaI, etaT, wi, value, pdf, isDeltaBxdf, lightingContext );
        pdf *= Wr;
    }
    else
    {
        SampleCookTorranceMicrofacetBTDF( wo, bxdfSample, color, alpha, etaI, etaT, wi, value, pdf, isDeltaBxdf, lightingContext );
        pdf *= 1.0f - Wr;
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

float MultiscatteringFresnel( float eta, float Eavg )
{
    float Favg = MultiscatteringFavgDielectric( eta );
    return Favg * Favg * Eavg / ( 1.0f - Favg * ( 1.0f - Eavg ) );
}

float MultiscatteringBxDF( float Ei, float Eo, float Eavg )
{
    return Eavg < 1.0f
        ? ( 1.0f - Ei ) * ( 1.0f - Eo ) / ( PI * ( 1.0f - Eavg ) )
        : 0.0f;
}

float3 CookTorranceBSDFMultiscatteringBxDFSampleHemisphere( float2 sample, float alpha, float eta )
{
    float cosThetaI = SampleCookTorranceMicrofacetBSDFInvCDFTexture( sample.x, alpha, eta );
    float phi = 2.0f * PI * sample.y;
    float s = sqrt( 1 - cosThetaI * cosThetaI );
    return float3( cos( phi ) * s, sin( phi ) * s, cosThetaI );
}

float EvaluateCookTorranceMicrofacetMultiscatteringBxDF( float3 wi, float3 color, float alpha, float eta, float Eo, float Eavg, float factor )
{
    float cosThetaI = abs( wi.z );
    if ( cosThetaI == 0.0f )
        return 0.0f;

    float Ei = SampleCookTorranceMicrofacetBSDFEnergyTexture( cosThetaI, alpha, eta );
    return MultiscatteringBxDF( Ei, Eo, Eavg ) * factor * color;
}

float EvaluateCookTorranceMicrofacetMultiscatteringBxDFPdf( float3 wi, float alpha, float eta )
{
    float cosThetaI = abs( wi.z );
    if ( cosThetaI == 0.0f )
        return 0.0f;

    float pdfScale = SampleCookTorranceMicrofacetBSDFPDFScaleTexture( alpha, eta );
    float Ei = SampleCookTorranceMicrofacetBSDFEnergyTexture( cosThetaI, alpha, eta );
    return pdfScale > 0.0f 
        ? ( 1.0f - Ei ) * cosThetaI / pdfScale 
        : 0.0f;
}

void SampleCookTorranceMicrofacetMultiscatteringBxDF( float3 wo, float2 sample, float3 color, float alpha, float eta, float Eo, float Eavg, float factor, out float3 wi, out float3 value, out float pdf, LightingContext lightingContext )
{
    value = 0.0f;
    pdf   = 0.0f;

    wi    = CookTorranceBSDFMultiscatteringBxDFSampleHemisphere( sample, alpha, eta );

    LightingContextCalculateH( wo, wi, lightingContext );
    lightingContext.WIdotN = wi.z;

    float cosThetaI = wi.z;
    if ( cosThetaI == 0.0f )
        return;

    float Ei = SampleCookTorranceMicrofacetBSDFEnergyTexture( cosThetaI, alpha, eta );
    value = MultiscatteringBxDF( Ei, Eo, Eavg ) * factor * color;

    float pdfScale = SampleCookTorranceMicrofacetBSDFPDFScaleTexture( alpha, eta );
    pdf = pdfScale > 0.0f
        ? ( 1.0f - Ei ) * cosThetaI / pdfScale
        : 0.0f;
}

//
// Multiscattering BSDF
//

float ReciprocalFactor( float Favg, float Favg_inv, float Eavg, float Eavg_inv, float eta )
{
    if ( Eavg == 1.0f || Eavg_inv == 1.0f )
        return 0.0f;

    float eta2 = eta * eta;
    float factor = 1.0f - Favg_inv;
    float factor1 = 1.0f - Eavg;
    float factor2 = 1.0f - Favg;
    float factor3 = 1.0f - Eavg_inv;
    float denom = factor2 / factor3 + factor * eta2 / factor1;
    float x = factor * eta2 / ( factor1 * denom );
    return eta > 1 ? x : ( 1.0f - x );
}

float3 EvaluateCookTorranceMicrofacetMultiscatteringBSDF( float3 wi, float3 wo, float3 color, float alpha, float etaI, float etaT, LightingContext lightingContext )
{
    float cosThetaO  = wo.z;
    float eta        = etaT / etaI;
    float Ebsdf      = SampleCookTorranceMicrofacetBSDFEnergyTexture( cosThetaO, alpha, eta );
    float Favg       = MultiscatteringFavgDielectric( eta );
    float invEta     = 1.0f / eta;
    float Eavg_inv   = SampleCookTorranceMicrofacetBSDFAverageEnergyTexture( alpha, invEta );
    float Favg_inv   = MultiscatteringFavgDielectric( invEta );
    float Eavg       = SampleCookTorranceMicrofacetBSDFAverageEnergyTexture( alpha, eta );
    float reciprocalFactor = ReciprocalFactor( Favg, Favg_inv, Eavg, Eavg_inv, eta );

    float3 value;
    if ( wi.z < 0.0f )
    {
        value = EvaluateCookTorranceMircofacetBTDF( wi, wo, color, alpha, etaI, etaT, lightingContext )
              + EvaluateCookTorranceMicrofacetMultiscatteringBxDF( wi, color, alpha, invEta, Ebsdf, Eavg_inv, reciprocalFactor * ( 1.0f - Favg ) );
    }
    else
    {
        value = EvaluateCookTorranceMircofacetBRDF( wi, wo, color, alpha, etaI, etaT, lightingContext )
              + EvaluateCookTorranceMicrofacetMultiscatteringBxDF( wi, color, alpha, eta, Ebsdf, Eavg, Favg );
    }
    return value;
}

float EvaluateCookTorranceMicrofacetMultiscatteringBSDFPdf( float3 wi, float3 wo, float alpha, float etaI, float etaT, LightingContext lightingContext )
{
    float pdf;
    pdf = wi.z < 0.0f
        ? EvaluateCookTorranceMicrofacetBTDFPdf( wi, wo, alpha, etaI, etaT, lightingContext ) + EvaluateCookTorranceMicrofacetMultiscatteringBxDFPdf( wi, alpha, etaI / etaT )
        : EvaluateCookTorranceMicrofacetBRDFPdf( wi, wo, alpha, lightingContext ) + EvaluateCookTorranceMicrofacetMultiscatteringBxDFPdf( wi, alpha, etaT / etaI );
    return pdf;
}

void SampleCookTorranceMicrofacetMultiscatteringBSDF( float3 wo, float selectionSample, float2 bxdfSample, float3 color, float alpha, float etaI, float etaT, out float3 wi, out float3 value, out float pdf, out bool isDeltaBxdf, inout LightingContext lightingContext )
{
    isDeltaBxdf = false;

    float cosThetaO  = wo.z;
    float eta        = etaT / etaI;
    float invEta     = 1.0f / eta;
    float Efresnel_r = SampleCookTorranceMicrofacetBRDFEnergyFresnelDielectricTexture( cosThetaO, alpha, eta );
    float Ebsdf      = SampleCookTorranceMicrofacetBSDFEnergyTexture( cosThetaO, alpha, eta );
    float Efresnel_t = SampleCookTorranceMicrofacetBTDFEnergyTexture( cosThetaO, alpha, eta ) * invEta * invEta;
    float Favg       = MultiscatteringFavgDielectric( eta );
    float Eavg_inv   = SampleCookTorranceMicrofacetBSDFAverageEnergyTexture( alpha, invEta );
    float Favg_inv   = MultiscatteringFavgDielectric( invEta );
    float Eavg       = SampleCookTorranceMicrofacetBSDFAverageEnergyTexture( alpha, eta );
    float reciprocalFactor = ReciprocalFactor( Favg, Favg_inv, Eavg, Eavg_inv, eta );
    float Ems_r      = ( 1.0f - Ebsdf ) * Favg;
    float Ems_t      = ( 1.0f - Ebsdf ) * ( 1.0f - Favg ) * reciprocalFactor;
    float Etotal     = Efresnel_r + Efresnel_t + Ems_r + Ems_t;

    float Wr = Efresnel_r / Etotal;
    float Wt = Efresnel_t / Etotal;
    float Wms_r = Ems_r / Etotal;
    float Wms_t = Ems_t / Etotal;

    if ( selectionSample < Wr )
    {
        SampleCookTorranceMicrofacetBRDF( wo, bxdfSample, color, alpha, etaI, etaT, wi, value, pdf, isDeltaBxdf, lightingContext );
        pdf *= Wr;
    }
    else if ( selectionSample < ( Wr + Wt ) )
    {
        SampleCookTorranceMicrofacetBTDF( wo, bxdfSample, color, alpha, etaI, etaT, wi, value, pdf, isDeltaBxdf, lightingContext );
        pdf *= Wt;
    }
    else if ( selectionSample < ( Wr + Wt + Wms_r ) )
    {
        SampleCookTorranceMicrofacetMultiscatteringBxDF( wo, bxdfSample, color, alpha, eta, Ebsdf, Eavg, Favg, wi, value, pdf, lightingContext );
        pdf *= Wms_r;
    }
    else
    {
        SampleCookTorranceMicrofacetMultiscatteringBxDF( wo, bxdfSample, color, alpha, invEta, Ebsdf, Eavg_inv, reciprocalFactor * ( 1.0f - Favg ), wi, value, pdf, lightingContext );
        pdf *= Wms_t;
    }
}

#endif