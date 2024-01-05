#ifndef _MICROFACET_BSDF_H_
#define _MICROFACET_BSDF_H_

#include "Math.inc.hlsl"
#include "LightingContext.inc.hlsl"
#include "MonteCarlo.inc.hlsl"
#include "Fresnel.inc.hlsl"

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

float EvaluateCookTorranceMicrofacetBSDF( float3 wi, float3 wo, float alpha, float etaO, float etaI, LightingContext lightingContext )
{
    bool active = wo.z != 0.0f && wi.z != 0.0f;

    bool reflect = wi.z * wo.z > 0.0f;

    float3 m = normalize( wo * ( reflect ? 1.0f : etaO ) + wi * ( reflect ? 1.0f : etaI ) );

    m = m.z < 0.0f ? -m : m;

    float WIdotM = dot( wi, m );
    float WOdotM = dot( wo, m );

    float D = EvaluateGGXMicrofacetDistribution( m, alpha );
    float F = FresnelDielectric( WOdotM, etaO, etaI );
    float G = EvaluateGGXGeometricShadowing( wi, wo, m, alpha );

    float WIdotN = wi.z;
    float WOdotN = wo.z;

    if ( reflect )
    {
        return active ? F * D * G / ( 4.0f * abs( WIdotN ) * abs( WOdotN ) ) : 0.0f;
    }
    else
    {
        float sqrtDenom = etaO * WOdotM + etaI * WIdotM;
        float value = ( 1.0f - F ) * abs( D * G
                      * abs( WIdotM ) * abs( WOdotM ) 
#if defined( REFRACTION_NO_SCALE_FACTOR )
                      * etaI * etaI
#else
                      * etaO * etaO // etaI * etaI * ( ( etaO * etaO ) / ( etaI * etaI ) )
#endif
                      / ( WOdotN * WIdotN * sqrtDenom * sqrtDenom ) );
        return active ? value : 0.0f;
    }
}

float EvaluateCookTorranceMicrofacetBSDFPdf( float3 wi, float3 wo, float alpha, float etaO, float etaI, LightingContext lightingContext )
{
    bool active = wo.z != 0.0f && wi.z != 0.0f;

    bool reflect = wi.z * wo.z > 0.0f;

    float3 m = normalize( wo * ( reflect ? 1.0f : etaO ) + wi * ( reflect ? 1.0f : etaI ) );

    m = m.z < 0.0f ? -m : m;

    float WIdotM = dot( wi, m );
    float WOdotM = dot( wo, m );

    // Ensure consistent orientation.
    // (Can't see backfacing microfacet normal from the front and vice versa)
    active = active && ( WIdotM * wi.z > 0.0f && WOdotM * wo.z > 0.0f );

    float sqrtDenom = etaO * WOdotM + etaI * WIdotM;
    float dwh_dwi = reflect ? 1.0f / ( 4.0f * WIdotM ) : abs( ( etaI * etaI * WIdotM ) / ( sqrtDenom * sqrtDenom ) );

    float pdf = EvaluateGGXMicrofacetDistributionPdf( wo, m, alpha );

    float F = FresnelDielectric( WOdotM, etaO, etaI );

    return active ? pdf * ( reflect ? F : 1.0f - F ) * dwh_dwi : 0.0f;
}

void SampleCookTorranceMicrofacetBSDF( float3 wo, float selectionSample, float2 bxdfSample, float alpha, float etaO, float etaI, out float3 wi, inout LightingContext lightingContext )
{
    wi = 0.0f;

    if ( wo.z == 0.0f )
    {
        return;
    }

    if ( etaO == etaI )
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

    float F = FresnelDielectric( WOdotM, etaO, etaI );
    bool sampleReflection = selectionSample < F;
    if ( sampleReflection )
    {
        wi = -reflect( wo, m );
    }
    else
    {
        wi = refract( -wo, m, etaO / etaI );
    }
}

#endif