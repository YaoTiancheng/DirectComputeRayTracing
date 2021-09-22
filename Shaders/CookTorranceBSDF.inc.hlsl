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

#define ALPHA_THRESHOLD 0.00052441f

//
// Cook-Torrance microfacet BRDF
//

float3 EvaluateCookTorranceMircofacetBRDF( float3 wi, float3 wo, float3 reflectance, float alpha, float3 F, LightingContext lightingContext )
{
    float WIdotN = wi.z;
    float WOdotM = lightingContext.WOdotH;
    if ( WIdotN <= 0.0f || WOdotM <= 0.0f || lightingContext.isInverted )
        return 0.0f;

    float3 m = lightingContext.H;
    if ( all( m == 0.0f ) )
        return 0.0f;

    float WOdotN = wo.z;
    return reflectance * EvaluateGGXMicrofacetDistribution( m, alpha ) * EvaluateGGXGeometricShadowing( wi, wo, m, alpha ) * F / ( 4.0f * WIdotN * WOdotN );
}

float3 EvaluateCookTorranceMircofacetBRDF_Dielectric( float3 wi, float3 wo, float3 reflectance, float alpha, float etaI, float etaT, LightingContext lightingContext )
{
    if ( alpha >= ALPHA_THRESHOLD )
    {
        float WOdotM = lightingContext.WOdotH;
        float F = FresnelDielectric( WOdotM, etaI, etaT );
        return EvaluateCookTorranceMircofacetBRDF( wi, wo, reflectance, alpha, F, lightingContext );
    }
    else
    {
        return EvaluateSpecularBRDF( wi, wo );
    }
}

float3 EvaluateCookTorranceMircofacetBRDF_Conductor( float3 wi, float3 wo, float3 reflectance, float alpha, float3 etaI, float3 etaT, float3 k, LightingContext lightingContext )
{
    if ( alpha >= ALPHA_THRESHOLD )
    {
        float WOdotM = lightingContext.WOdotH;
        float3 F = FresnelConductor( WOdotM, etaI, etaT, k );
        return EvaluateCookTorranceMircofacetBRDF( wi, wo, reflectance, alpha, F, lightingContext );
    }
    else
    {
        return EvaluateSpecularBRDF( wi, wo );
    }
}

float3 EvaluateCookTorranceMircofacetBRDF_Schlick( float3 wi, float3 wo, float3 reflectance, float alpha, float3 F0, LightingContext lightingContext )
{
    if ( alpha >= ALPHA_THRESHOLD )
    {
        float WOdotM = lightingContext.WOdotH;
        float3 F = FresnelSchlick( WOdotM, F0 );
        return EvaluateCookTorranceMircofacetBRDF( wi, wo, reflectance, alpha, F, lightingContext );
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
        float WIdotN = wi.z;
        float WOdotM = lightingContext.WOdotH;
        if ( WIdotN <= 0.0f || WOdotM <= 0.0f || lightingContext.isInverted )
            return 0.0f;

        float3 m = lightingContext.H;
        float pdf = EvaluateGGXMicrofacetDistributionPdf( wo, m, alpha );
        return pdf / ( 4.0f * WOdotM );
    }
    else
    {
        return EvaluateSpecularBRDFPdf( wi, wo );
    }
}

void SampleCookTorranceMicrofacetBRDF_Dielectric( float3 wo, float2 sample, float3 reflectance, float alpha, float etaI, float etaT, out float3 wi, out float3 value, out float pdf, out bool isDeltaBrdf, inout LightingContext lightingContext )
{
    if ( alpha >= ALPHA_THRESHOLD )
    {
        float3 m;
        SampleGGXMicrofacetDistribution( wo, sample, alpha, m );
        wi = -reflect( wo, m );

        LightingContextAssignH( wo, m, lightingContext );

        float WOdotM = lightingContext.WOdotH;
        float F = FresnelDielectric( WOdotM, etaI, etaT );

        float WIdotN = wi.z;

        value = EvaluateCookTorranceMircofacetBRDF( wi, wo, reflectance, alpha, F, lightingContext );
        pdf = EvaluateCookTorranceMicrofacetBRDFPdf( wi, wo, alpha, lightingContext );

        isDeltaBrdf = false;
    }
    else
    {
        SampleSpecularBRDF_Dielectric( wo, reflectance, etaI, etaT, wi, value, pdf, lightingContext );
        isDeltaBrdf = true;
    }
}

void SampleCookTorranceMicrofacetBRDF_Conductor( float3 wo, float2 sample, float3 reflectance, float alpha, float3 etaI, float3 etaT, float3 k, out float3 wi, out float3 value, out float pdf, out bool isDeltaBrdf, inout LightingContext lightingContext )
{
    if ( alpha >= ALPHA_THRESHOLD )
    {
        float3 m;
        SampleGGXMicrofacetDistribution( wo, sample, alpha, m );
        wi = -reflect( wo, m );

        LightingContextAssignH( wo, m, lightingContext );

        float WOdotM = lightingContext.WOdotH;
        float3 F = FresnelConductor( WOdotM, etaI, etaT, k );

        float WIdotN = wi.z;

        value = EvaluateCookTorranceMircofacetBRDF( wi, wo, reflectance, alpha, F, lightingContext );
        pdf = EvaluateCookTorranceMicrofacetBRDFPdf( wi, wo, alpha, lightingContext );

        isDeltaBrdf = false;
    }
    else
    {
        SampleSpecularBRDF_Conductor( wo, reflectance, etaI, etaT, k, wi, value, pdf, lightingContext );
        isDeltaBrdf = true;
    }
}

void SampleCookTorranceMicrofacetBRDF_Schlick( float3 wo, float2 sample, float3 reflectance, float alpha, float3 F0, out float3 wi, out float3 value, out float pdf, out bool isDeltaBrdf, inout LightingContext lightingContext )
{
    if ( alpha >= ALPHA_THRESHOLD )
    {
        float3 m;
        SampleGGXMicrofacetDistribution( wo, sample, alpha, m );
        wi = -reflect( wo, m );

        LightingContextAssignH( wo, m, lightingContext );

        float WOdotM = lightingContext.WOdotH;
        float3 F = FresnelSchlick( WOdotM, F0 );

        float WIdotN = wi.z;

        value = EvaluateCookTorranceMircofacetBRDF( wi, wo, reflectance, alpha, F, lightingContext );
        pdf = EvaluateCookTorranceMicrofacetBRDFPdf( wi, wo, alpha, lightingContext );

        isDeltaBrdf = false;
    }
    else
    {
        SampleSpecularBRDF_Schlick( wo, reflectance, F0, wi, value, pdf, lightingContext );
        isDeltaBrdf = true;
    }
}

//
// Microfacet BTDF
//

float CookTorranceMicrofacetBTDF( float3 wi, float3 wo, float3 m, float alpha, float etaI, float etaT, float WIdotN, float WOdotN, float WIdotM, float WOdotM, float sqrtDenom )
{
    return sqrtDenom != 0 ? 
        ( 1.0f - FresnelDielectric( WIdotM, etaI, etaT ) ) * abs( EvaluateGGXMicrofacetDistribution( m, alpha ) * EvaluateGGXGeometricShadowing( wi, wo, m, alpha )
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
        float WIdotN = wi.z;
        float WOdotN = wo.z;
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
        float WOdotN = wo.z;
        if ( WOdotN == 0.0f )
            return;

        float3 m;
        SampleGGXMicrofacetDistribution( wo, sample, alpha, m );
        wi = refract( -wo, m, etaI / etaT );
        if ( all( wi == 0.0f ) )
            return;

        LightingContextAssignH( wo, m, lightingContext );

        float WIdotN = wi.z;

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
    {
        return EvaluateCookTorranceMircofacetBTDF( wi, wo, color, alpha, etaI, etaT, lightingContext );
    }
    else
    {
        // Need to make the BRDF as two sided.
        LightingContext lightingContext1 = lightingContext;
        lightingContext1.isInverted = false;
        return EvaluateCookTorranceMircofacetBRDF_Dielectric( wi, wo, color, alpha, etaI, etaT, lightingContext1 );
    }
}

float EvaluateCookTorranceMicrofacetBSDFPdf( float3 wi, float3 wo, float alpha, float etaI, float etaT, LightingContext lightingContext )
{
    if ( wi.z < 0.0f )
    {
        return EvaluateCookTorranceMicrofacetBTDFPdf( wi, wo, alpha, etaI, etaT, lightingContext );
    }
    else
    {
        // Need to make the BRDF as two sided.
        LightingContext lightingContext1 = lightingContext;
        lightingContext1.isInverted = false;
        return EvaluateCookTorranceMicrofacetBRDFPdf( wi, wo, alpha, lightingContext1 );
    }
}

void SampleCookTorranceMicrofacetBSDF( float3 wo, float selectionSample, float2 bxdfSample, float3 color, float alpha, float etaI, float etaT, out float3 wi, out float3 value, out float pdf, out bool isDeltaBxdf, inout LightingContext lightingContext )
{
    value = 0.0f;
    pdf   = 0.0f;
    wi    = 0.0f;

    if ( wo.z == 0.0f )
        return;

    if ( etaI == etaT )
    {
        value = color / wo.z;
        pdf = 1.0f;
        wi = -wo;
        return;
    }

    isDeltaBxdf = alpha < ALPHA_THRESHOLD;

    float G1_o = 0.0f;
    float D    = 0.0f;
    float alpha2 = alpha * alpha;

    float3 m;
    float WOdotM;
    if ( !isDeltaBxdf )
    {
        SampleGGXMicrofacetDistribution( wo, bxdfSample, alpha, m );
        D = EvaluateGGXMicrofacetDistribution( m, alpha );
        G1_o = EvaluateGGXGeometricShadowingOneDirection( alpha2, m, wo );
        WOdotM = dot( wo, m );
#if defined( GGX_SAMPLE_VNDF )
        pdf = D * G1_o * WOdotM / wo.z;
#else
        pdf = D * abs( m.z );
#endif
    }
    else
    {
        m = float3( 0, 0, 1 );
        WOdotM = wo.z;
        pdf = 1.0f;
    }

    lightingContext.H = m;
    lightingContext.WOdotH = WOdotM;

    if ( WOdotM <= 0.0f )
        return;

    float F = FresnelDielectric( WOdotM, etaI, etaT );
    float Wr = F;

    float WIdotM;
    bool sampleReflection = selectionSample < Wr;
    if ( sampleReflection )
    {
        wi = -reflect( wo, m );
        
        if ( wi.z <= 0.0f )
            return;

        WIdotM = dot( wi, m );
        pdf *= Wr;
    }
    else
    {
        wi = refract( -wo, m, etaI / etaT );

        if ( wi.z == 0.0f )
            return;

        WIdotM = dot( wi, m );
        F = FresnelDielectric( WIdotM, etaI, etaT );
        pdf *= 1.0f - Wr;
    }

    float WIdotN = wi.z;
    float WOdotN = wo.z;

    if ( !isDeltaBxdf )
    {
        float G1_i = EvaluateGGXGeometricShadowingOneDirection( alpha2, m, wi );
        float G = G1_o * G1_i;

        float dwh_dwi;
        if ( sampleReflection )
        {
            value = D * G * F / ( 4.0f * WIdotN * WOdotN );
            dwh_dwi = 1.0f / ( 4.0f * WOdotM );
        }
        else
        {
            float sqrtDenom = etaI * WOdotM + etaT * WIdotM;
            value = ( 1.0f - F ) * abs( D * G * abs( WIdotM ) * abs( WOdotM ) * etaI * etaI / ( WOdotN * WIdotN * sqrtDenom * sqrtDenom ) );
            dwh_dwi = abs( ( etaT * etaT * WIdotM ) / ( sqrtDenom * sqrtDenom ) );
        }

        pdf *= dwh_dwi;
    }
    else
    {
        if ( sampleReflection )
        {
            value = F / wi.z;
        }
        else
        {
            value = ( 1.0f - F ) * ( etaI * etaI ) / ( etaT * etaT ) / ( -wi.z );
        }
    }
    
    value *= color;
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

float3 MultiscatteringFavgConductor( float eta, float3 k )
{
    bool inverted = eta < 1.0f;
    float a = inverted ? -3.6128759661233995E-01 :  1.0763689472730897E-01;
    float b = inverted ?  5.6976345192125910E-01 : -2.0650405955495116E-01;
    float c = inverted ?  2.4033861143029422E-01 :  3.7283476597976423E-01;
    float d = inverted ? -1.5542095297540026E-02 :  1.4989904095045403E-01;
    float f = inverted ?  1.7634798639250104E-01 :  5.3962649152329845E-03;
    float g = inverted ? -9.7938770603064607E-03 : -1.9900529006203940E-02;
    float h = inverted ? -3.5207291851450107E-02 : -7.8936303193975904E-03;
    float i = inverted ? -4.2813238653964769E-01 : -1.5808125122638464E-01;
    float j = inverted ?  4.2004665746286168E-02 :  6.1371079326111022E-03;
    float k_ = inverted ? 3.8521674898567504E-02 :  1.9186529069727634E-02;
    float eta2 = eta * eta;
    float eta3 = eta2 * eta;
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
    return temp;
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

float EvaluateCookTorranceMultiscatteringBSDF( float3 wi, float3 color, float alpha, float eta, float Eo, float Eavg, float factor )
{
    float cosThetaI = abs( wi.z );
    if ( cosThetaI == 0.0f )
        return 0.0f;

    float Ei = SampleCookTorranceMicrofacetBSDFEnergyTexture( cosThetaI, alpha, eta );
    return MultiscatteringBxDF( Ei, Eo, Eavg ) * factor * color;
}

float EvaluateCookTorranceMultiscatteringBSDFPdf( float3 wi, float alpha, float eta )
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

void SampleCookTorranceMultiscatteringBSDF( float3 wo, float selectionSample, float2 bxdfSample, float3 color, float alpha, float Ems_r, float Ems_t, float Eo, float eta, float invEta, float Eavg, float Eavg_inv, float Favg, float reciprocalFactor, out float3 wi, out float3 value, out float pdf, LightingContext lightingContext )
{
    value = 0.0f;
    pdf   = 0.0f;
    wi    = 0.0f;

    if ( wo.z == 0.0f )
        return;

    float Wr = Ems_r / ( Ems_r + Ems_t );
    bool sampleReflection = selectionSample < Wr;

    eta = sampleReflection ? eta : invEta;

    wi = CookTorranceMultiscatteringBSDFSampleHemisphere( bxdfSample, alpha, eta );

    if ( !sampleReflection )
        wi.z = -wi.z;

    LightingContextCalculateH( wo, wi, lightingContext );

    float cosThetaI = abs( wi.z );
    if ( cosThetaI == 0.0f )
        return;

    float factor = sampleReflection ? Favg : ( 1 - Favg ) * reciprocalFactor;
    float Ei = SampleCookTorranceMicrofacetBSDFEnergyTexture( cosThetaI, alpha, eta );
    Eavg = sampleReflection ? Eavg : Eavg_inv;
    value = MultiscatteringBxDF( Ei, Eo, Eavg ) * factor * color;

    float pdfScale = SampleCookTorranceMicrofacetBSDFPDFScaleTexture( alpha, eta );
    pdf = pdfScale > 0.0f
        ? ( 1.0f - Ei ) * cosThetaI / pdfScale
        : 0.0f;
    pdf *= sampleReflection ? Wr : 1 - Wr;
}

//
// Microfacet Multiscattering BSDF
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
              + EvaluateCookTorranceMultiscatteringBSDF( wi, color, alpha, invEta, Ebsdf, Eavg_inv, reciprocalFactor * ( 1.0f - Favg ) );
    }
    else
    {
        // Need to make the BRDF as two sided.
        LightingContext lightingContext1 = lightingContext;
        lightingContext1.isInverted = false;
        value = EvaluateCookTorranceMircofacetBRDF_Dielectric( wi, wo, color, alpha, etaI, etaT, lightingContext1 )
              + EvaluateCookTorranceMultiscatteringBSDF( wi, color, alpha, eta, Ebsdf, Eavg, Favg );
    }
    return value;
}

float EvaluateCookTorranceMicrofacetMultiscatteringBSDFPdf( float3 wi, float3 wo, float alpha, float etaI, float etaT, LightingContext lightingContext )
{
    // Need to make the BRDF as two sided.
    LightingContext lightingContext1 = lightingContext;
    lightingContext1.isInverted = false;
    float pdf;
    pdf = wi.z < 0.0f
        ? EvaluateCookTorranceMicrofacetBTDFPdf( wi, wo, alpha, etaI, etaT, lightingContext ) + EvaluateCookTorranceMultiscatteringBSDFPdf( wi, alpha, etaI / etaT )
        : EvaluateCookTorranceMicrofacetBRDFPdf( wi, wo, alpha, lightingContext1 ) + EvaluateCookTorranceMultiscatteringBSDFPdf( wi, alpha, etaT / etaI );
    return pdf;
}

void SampleCookTorranceMicrofacetMultiscatteringBSDF( float3 wo, float selectionSample, float2 bxdfSample, float3 color, float alpha, float etaI, float etaT, out float3 wi, out float3 value, out float pdf, out bool isDeltaBxdf, inout LightingContext lightingContext )
{
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

    float weight = ( Efresnel_r + Efresnel_t ) / ( Efresnel_r + Efresnel_t + Ems_r + Ems_t );
    bool sampleMicrofacet = selectionSample < weight;
    if ( sampleMicrofacet )
    {
        SampleCookTorranceMicrofacetBSDF( wo, selectionSample / weight, bxdfSample, color, alpha, etaI, etaT, wi, value, pdf, isDeltaBxdf, lightingContext );
        pdf *= weight;
    }
    else
    {
        SampleCookTorranceMultiscatteringBSDF( wo, ( selectionSample - weight ) / ( 1 - weight ), bxdfSample, color, alpha, Ems_r, Ems_t, Ebsdf, eta, invEta, Eavg, Eavg_inv, Favg, reciprocalFactor, wi, value, pdf, lightingContext );
        pdf *= 1 - weight;
        isDeltaBxdf = false;
    }
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

float3 EvaluateCookTorranceMultiscatteringBRDF( float3 wi, float3 wo, float3 color, float alpha, float Eo, float Eavg, float3 factor, LightingContext lightingContext )
{
    float cosThetaI = wi.z;
    if ( cosThetaI <= 0.0f || wo.z == 0.0f || lightingContext.isInverted )
        return 0.0f;

    float Ei = SampleCookTorranceMicrofacetBRDFEnergyTexture( cosThetaI, alpha );
    return MultiscatteringBxDF( Ei, Eo, Eavg ) * factor * color;
}

float EvaluateCookTorranceMultiscatteringBRDFPdf( float3 wi, float3 wo, float alpha, LightingContext lightingContext )
{
    float cosThetaI = wi.z;
    if ( cosThetaI <= 0.0f || wo.z == 0.0f || lightingContext.isInverted )
        return 0.0f;

    float pdfScale = SampleCookTorranceMicrofacetBRDFPDFScaleTexture( alpha );
    float Ei = SampleCookTorranceMicrofacetBRDFEnergyTexture( cosThetaI, alpha );
    return pdfScale > 0.0f
        ? ( 1.0f - Ei ) * cosThetaI / pdfScale
        : 0.0f;
}

void SampleCookTorranceMultiscatteringBRDF( float3 wo, float2 bxdfSample, float3 color, float alpha, float Eo, float Eavg, float3 Fms, out float3 wi, out float3 value, out float pdf, inout LightingContext lightingContext )
{
    value = 0.0f;
    pdf = 0.0f;
    wi = 0.0f;

    if ( wo.z == 0.0f || lightingContext.isInverted )
        return;

    wi = CookTorranceMultiscatteringBRDFSampleHemisphere( bxdfSample, alpha );

    LightingContextCalculateH( wo, wi, lightingContext );

    float cosThetaI = wi.z;
    if ( cosThetaI <= 0.0f )
        return;

    float Ei = SampleCookTorranceMicrofacetBRDFEnergyTexture( cosThetaI, alpha );
    value = MultiscatteringBxDF( Ei, Eo, Eavg ) * Fms * color;

    float pdfScale = SampleCookTorranceMicrofacetBRDFPDFScaleTexture( alpha );
    pdf = pdfScale > 0.0f
        ? ( 1.0f - Ei ) * cosThetaI / pdfScale
        : 0.0f;
}

#endif