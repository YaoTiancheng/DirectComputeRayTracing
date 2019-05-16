
//
// Lambert BRDF
//

float4 ConsineSampleHemisphere( float2 sample )
{
    sample = ConcentricSampleDisk( sample );
    return float4( sample.xy, sqrt( max( 0.0f, 1.0f - dot( sample.xy, sample.xy ) ) ), 0.0f );
}

float4 EvaluateLambertBRDF( float4 wi, float4 albedo )
{
    return wi.z > 0.0f ? albedo * INV_PI : 0.0f;
}

float EvaluateLambertBRDFPdf( float4 wi )
{
    return max( 0.0f, wi.z * INV_PI );
}

void SampleLambertBRDF( float4 wo
	, float2 sample
    , float4 albedo
    , out float4 wi
    , out float4 value
    , out float pdf )
{
    wi = ConsineSampleHemisphere( sample );
    value = EvaluateLambertBRDF( wi, albedo );
    pdf = EvaluateLambertBRDFPdf( wi );
}

//
// GGX microfacet distribution
//

float4 GGXSampleHemisphere( float2 sample, float alpha )
{
    float theta = atan( alpha * sqrt( sample.x / ( 1.0f - sample.x ) ) );
    float phi = 2.0f * PI * sample.y;

    float s = sin( theta );
    return float4( cos( phi ) * s, sin( phi ) * s, cos( theta ), 0.0f );
}

float EvaluateGGXMicrofacetDistribution( float4 m, float alpha )
{
    float alpha2 = alpha * alpha;
    float NdotM = m.z;
    float NdotM2 = NdotM * NdotM;
    float factor = NdotM2 * ( alpha2 - 1.0f ) + 1.0f;
    float denominator = factor * factor * PI;
    return alpha2 / denominator;
}

float EvaluateGGXMicrofacetDistributionPdf( float4 m, float alpha )
{
    return EvaluateGGXMicrofacetDistribution( m, alpha ) * m.z;
}

void SampleGGXMicrofacetDistribution( float2 sample, float alpha, out float4 m, out float pdf )
{
    m = GGXSampleHemisphere( sample, alpha );
    pdf = EvaluateGGXMicrofacetDistributionPdf( m, alpha );
}

void SampleGGXMicrofacetDistribution( float2 sample, float alpha, out float4 m )
{
    m = GGXSampleHemisphere( sample, alpha );
}

//
// GGX geometric shadowing
//

float EvaluateGGXGeometricShadowingOneDirection( float alpha2, float4 w )
{
    float NdotW = abs( w.z );
    float denominator = sqrt( alpha2 + ( 1.0f - alpha2 ) * NdotW * NdotW ) + NdotW;
    return 2.0f * NdotW / denominator;
}

float EvaluateGGXGeometricShadowing( float4 wi, float4 wo, float alpha )
{
    float alpha2 = alpha * alpha;
    return EvaluateGGXGeometricShadowingOneDirection( alpha2, wi ) * EvaluateGGXGeometricShadowingOneDirection( alpha2, wo );
}

float4 EvaluateFresnelF0( float4 ior )
{
    float f0 = ( 1.0f - ior ) / ( 1.0f + ior );
    return f0 * f0;
}

float4 FresnelAverage( float4 ior )
{
    return ( ior - 1.0f ) / ( 4.08567f + 1.00071f * ior );
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

float4 EvaluateCookTorranceFresnel( float WOdotM, float4 ior )
{
    float4 g = sqrt( ior * ior + WOdotM * WOdotM - 1.0f );
    float4 term0 = g - WOdotM;
    float4 term1 = g + WOdotM;
    float4 term2 = term0 / term1;
    float4 term3 = ( term1 * WOdotM - 1.0f ) / ( term0 * WOdotM + 1.0f );
    return 0.5f * term2 * term2 * ( 1.0f -  term3 * term3 );
}

//
// Cook-Torrance microfacet BRDF
//

float4 EvaluateCookTorranceMircofacetBRDF( float4 wi, float4 wo, float4 reflectance, float alpha, float ior )
{
    float WIdotN = wi.z;
    float WOdotN = wo.z;
    if ( WIdotN <= 0.0f || WOdotN <= 0.0f )
        return 0.0f;

    float4 m = wi + wo;
    if ( all( m == 0.0f ) )
        return 0.0f;
    m = normalize( m );

    float WOdotM = abs( dot( wo, m ) );
    return reflectance * EvaluateGGXMicrofacetDistribution( m, alpha ) * EvaluateGGXGeometricShadowing( wi, wo, alpha ) * EvaluateCookTorranceFresnel( WOdotM, ior ) / ( 4.0f * WIdotN * WOdotN );
}

float EvaluateCookTorranceMicrofacetBRDFPdf( float4 wi, float4 wo, float alpha )
{
    float WIdotN = wi.z;
    float WOdotN = wo.z;
    if ( WIdotN <= 0.0f || WOdotN <= 0.0f )
        return 0.0f;
    
    float4 m = normalize( wi + wo );
    float  pdf = EvaluateGGXMicrofacetDistributionPdf( m, alpha );
    return pdf / ( 4.0f * dot( wi, m ) );
}

void SampleCookTorranceMicrofacetBRDF( float4 wo, float2 sample, float4 reflectance, float alpha, float ior, out float4 wi, out float4 value, out float pdf )
{
    float WOdotN = wo.z;
    if ( WOdotN <= 0.0f )
    {
        value = 0.0f;
        pdf = 0.0f;
        return;
    }

    float4 m;
    SampleGGXMicrofacetDistribution( sample, alpha, m );
    wi = -reflect( wo, m );

    float WIdotN = wi.z;
    if ( WIdotN <= 0.0f )
    {
        value = 0.0f;
        pdf = 0.0f;
        return;
    }
    
    value = EvaluateCookTorranceMircofacetBRDF( wi, wo, reflectance, alpha, ior );
    pdf   = EvaluateCookTorranceMicrofacetBRDFPdf( wi, wo, alpha );
}

//
// Cook-Torrance Compensation
//

cbuffer CookTorranceCompTextureConstants : register( b0 )
{
    float4 g_CompETextureSize;
    float4 g_CompEAvgTextureSize;
    float4 g_CompInvCDFTextureSize;
    float4 g_CompPdfScaleTextureSize;
}

Texture2D g_CookTorranceCompETexture : register( t4 );
Texture2D g_CookTorranceCompEAvgTexture : register( t5 );
Texture2D g_CookTorranceCompInvCDFTexture : register( t6 );
Texture2D g_CookTorranceCompPdfScaleTexture : register( t7 );

SamplerState UVClampSampler;

float EvaluateCookTorranceCompE( float cosTheta, float alpha )
{
    float2 texelPos = float2( cosTheta, alpha ) * ( g_CompETextureSize.xy - 1.0f );
    float2 uv = ( texelPos + 0.5f ) * g_CompETextureSize.zw;
    float2 fraction = frac( texelPos );
    float4 values = g_CookTorranceCompETexture.Gather( UVClampSampler, uv );
    float2 value = lerp( values.wx, values.zy, fraction.x );
    return lerp( value.x, value.y, fraction.y );
}

float EvaluateCookTorranceCompEAvg( float alpha )
{
    float2 texelPos = float2( alpha, 0.0f ) * ( g_CompEAvgTextureSize.xy - 1.0f );
    float2 uv = ( texelPos + 0.5f ) * g_CompEAvgTextureSize.zw;
    float4 values = g_CookTorranceCompEAvgTexture.Gather( UVClampSampler, uv );
    float fraction = frac( texelPos.x );
    return lerp( values.w, values.z, fraction );

    //float alpha2 = alpha * alpha;
    //float alpha3 = alpha2 * alpha;
    //float alpha4 = alpha2 * alpha2;
    //float alpha5 = alpha4 * alpha;
    //return 1.00512442f - 0.11188488f * alpha - 2.18890995f * alpha2 + 3.26510361f * alpha3 - 2.19288381f * alpha4 + 0.60181649f * alpha5;
}

float EvaluateCookTorranceCompInvCDF( float sample, float alpha )
{
    float2 texelPos = float2( sample, alpha ) * ( g_CompInvCDFTextureSize.xy - 1.0f );
    float2 uv = ( texelPos + 0.5f ) * g_CompInvCDFTextureSize.zw;
    float2 fraction = frac( texelPos );
    float4 values = g_CookTorranceCompInvCDFTexture.Gather( UVClampSampler, uv );
    float2 value = lerp( values.wx, values.zy, fraction.x );
    return lerp( value.x, value.y, fraction.y );
}

float EvaluateCookTorranceCompPdfScale( float alpha )
{
    float2 texelPos = float2( alpha, 0.0f ) * ( g_CompPdfScaleTextureSize.xy - 1.0f );
    float2 uv = ( texelPos + 0.5f ) * g_CompPdfScaleTextureSize.zw;
    float4 values = g_CookTorranceCompPdfScaleTexture.Gather( UVClampSampler, uv );
    float fraction = frac( texelPos.x );
    return lerp( values.w, values.z, fraction ) * 2.0f;

    //float alpha2 = alpha * alpha;
    //float alpha3 = alpha2 * alpha;
    //float alpha4 = alpha2 * alpha2;
    //float alpha5 = alpha4 * alpha;
    //return -0.0039323f + 0.35268906f * alpha + 6.88429598f * alpha2 - 10.29970127f * alpha3 + 6.94830677f * alpha4 - 1.91625088f * alpha5;
}

float4 CookTorranceCompSampleHemisphere( float2 sample, float alpha )
{
    //return ConsineSampleHemisphere(sample);
    float cosTheta = EvaluateCookTorranceCompInvCDF( sample.x, alpha );
    float phi = 2.0f * PI * sample.y;
    float s = sqrt( 1 - cosTheta * cosTheta );
    return float4( cos( phi ) * s, sin( phi ) * s, cosTheta, 0.0f );
}

float4 EvaluateCookTorranceCompFresnel( float4 ior, float EAvg )
{
    //float4 f0 = EvaluateFresnelF0( ior );
    //float  f02 = f0 * f0;
    //float  f03 = f02 * f0;
    //return 0.04f * f0 + 0.66f * f02 + 0.3f * f03;

    float4 FAvg = FresnelAverage( ior );
    return FAvg * EAvg / ( 1.0f - FAvg * ( 1.0f - EAvg ) );
}

float4 EvaluateCookTorranceCompBRDF( float4 wi, float4 wo, float4 reflectance, float alpha, float4 ior )
{
    float WIdotN = wi.z;
    float WOdotN = wo.z;
    if ( WIdotN <= 0.0f || WOdotN <= 0.0f )
        return 0.0f;

    float eI = EvaluateCookTorranceCompE( WIdotN, alpha );
    float eO = EvaluateCookTorranceCompE( WOdotN, alpha );
    float eAvg = EvaluateCookTorranceCompEAvg( alpha );
    float4 fresnel = EvaluateCookTorranceCompFresnel( ior, eAvg );
    return reflectance * ( 1.0f - eI ) * ( 1.0f - eO ) * fresnel / ( PI * ( 1.0f - eAvg ) );
}

float EvaluateCookTorranceCompPdf( float4 wi, float alpha )
{
    //return EvaluateLambertBRDFPdf(wi);
    float cosTheta = wi.z;
    if ( cosTheta < 0.0f )
        return 0.0f;

    float pdfScale = EvaluateCookTorranceCompPdfScale( alpha );
    return ( 1.0f - EvaluateCookTorranceCompE( cosTheta, alpha ) ) * cosTheta / pdfScale;
}

void SampleCookTorranceCompBRDF( float4 wo, float2 sample, float4 reflectance, float alpha, float4 ior, out float4 wi, out float4 value, out float pdf )
{
    wi = CookTorranceCompSampleHemisphere( sample, alpha );
    value = EvaluateCookTorranceCompBRDF( wi, wo, reflectance, alpha, ior );
    pdf = EvaluateCookTorranceCompPdf( wi, alpha );
}

//
// Cook-Torrance BTDF
//

float EvaluateCookTorranceMicrofacetBTDFPdf( float4 wi, float4 wo, float4 m, float alpha, float no, float ni )
{
    float WIdotM = abs( dot( wi, m ) );
    float WOdotM = abs( dot( wo, m ) );
    float pdf = EvaluateGGXMicrofacetDistributionPdf( m, alpha );
    float term = no * WOdotM + ni * WIdotM;
    return pdf * WOdotM * ni * ni / ( term * term );
}

float4 SpecularWeight( float4 ior, float E )
{
    return FresnelAverage( ior ) * E;
}

float4 SpecularCompWeight( float4 ior, float E, float EAvg )
{
    return EvaluateCookTorranceCompFresnel( ior, EAvg ) * ( 1.0f - E );
}

//
// BSDF
// 

float4 EvaluateBSDF( float4 wi, float4 wo, Intersection intersection )
{
    float4 biNormal = float4( cross( intersection.tangent.xyz, intersection.normal.xyz ), 0.0f );
    float4x4 tbn2world = float4x4( intersection.tangent, biNormal, intersection.normal, float4( 0.0f, 0.0f, 0.0f, 1.0f ) );
    float4x4 world2tbn = transpose( tbn2world );

    wo = mul( wo, world2tbn );
    wi = mul( wi, world2tbn );

    float4 value = EvaluateCookTorranceMircofacetBRDF( wi, wo, intersection.specular, intersection.alpha, intersection.ior );
    value += EvaluateCookTorranceCompBRDF( wi, wo, intersection.specular, intersection.alpha, intersection.ior );

    float E = EvaluateCookTorranceCompE( wo.z, intersection.alpha );
    float EAvg = EvaluateCookTorranceCompEAvg( intersection.alpha );
    float specularWeight        = SpecularWeight( intersection.ior, E );
    float specularCompWeight    = SpecularCompWeight( intersection.ior, E, EAvg );
    float diffuseWeight = 1.0f - specularWeight - specularCompWeight;
    value += EvaluateLambertBRDF( wi, intersection.albedo ) * diffuseWeight;

    return value;
}

void SampleBSDF( float4 wo
    , float2 BRDFSample
    , float BRDFSelectionSample
    , Intersection intersection
    , out float4 wi
    , out float4 value
    , out float pdf )
{
    float4 biNormal = float4( cross( intersection.tangent.xyz, intersection.normal.xyz ), 0.0f );
    float4x4 tbn2world = float4x4( intersection.tangent, biNormal, intersection.normal, float4( 0.0f, 0.0f, 0.0f, 1.0f ) );
    float4x4 world2tbn = transpose( tbn2world );

    wo = mul( wo, world2tbn );

    float cosThetaO = wo.z;
    float E = EvaluateCookTorranceCompE( cosThetaO, intersection.alpha );
    float EAvg = EvaluateCookTorranceCompEAvg( intersection.alpha );
    float specularWeight        = SpecularWeight( intersection.ior, E );
    float specularCompWeight    = SpecularCompWeight( intersection.ior, E, EAvg );
    float diffuseWeight         = 1.0f - specularWeight - specularCompWeight;
    if ( BRDFSelectionSample < specularWeight )
    {
        SampleCookTorranceMicrofacetBRDF( wo, BRDFSample, intersection.specular, intersection.alpha, intersection.ior, wi, value, pdf );
        pdf *= specularWeight;

        value += EvaluateCookTorranceCompBRDF( wi, wo, intersection.specular, intersection.alpha, intersection.ior );
        pdf += EvaluateCookTorranceCompPdf( wi, intersection.alpha ) * specularCompWeight;

        value += EvaluateLambertBRDF( wi, intersection.albedo ) * diffuseWeight;
        pdf += EvaluateLambertBRDFPdf( wi ) * diffuseWeight;
    }
    else if ( BRDFSelectionSample < specularWeight + specularCompWeight )
    {
        SampleCookTorranceCompBRDF( wo, BRDFSample, intersection.specular, intersection.alpha, intersection.ior, wi, value, pdf );
        pdf *= specularCompWeight;

        value += EvaluateCookTorranceMircofacetBRDF( wi, wo, intersection.specular, intersection.alpha, intersection.ior );
        pdf += EvaluateCookTorranceMicrofacetBRDFPdf( wi, wo, intersection.alpha ) * specularWeight;

        value += EvaluateLambertBRDF( wi, intersection.albedo ) * diffuseWeight;
        pdf += EvaluateLambertBRDFPdf( wi ) * diffuseWeight;
    }
    else
    {
        SampleLambertBRDF( wo, BRDFSample, intersection.albedo, wi, value, pdf );
        value *= diffuseWeight;
        pdf *= diffuseWeight;

        value += EvaluateCookTorranceMircofacetBRDF( wi, wo, intersection.specular, intersection.alpha, intersection.ior );
        pdf += EvaluateCookTorranceMicrofacetBRDFPdf( wi, wo, intersection.alpha ) * specularWeight;

        value += EvaluateCookTorranceCompBRDF( wi, wo, intersection.specular, intersection.alpha, intersection.ior );
        pdf += EvaluateCookTorranceCompPdf( wi, intersection.alpha ) * specularCompWeight;
    }

    wi = mul( wi, tbn2world );
}
