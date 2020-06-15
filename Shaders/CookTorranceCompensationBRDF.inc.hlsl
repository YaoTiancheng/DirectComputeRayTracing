#ifndef _COOKTORRANCECOMPENSATIONBRDF_H_
#define _COOKTORRANCECOMPENSATIONBRDF_H_

#include "Math.inc.hlsl"

cbuffer CookTorranceCompTextureConstants : register( b0 )
{
    float4 g_CompETextureSize;
    float4 g_CompEAvgTextureSize;
    float4 g_CompInvCDFTextureSize;
    float4 g_CompPdfScaleTextureSize;
    float4 g_CompEFresnelTextureSize;
    float4 g_CompEFresnelTextureSizeRcp;
}

Texture2D g_CookTorranceCompETexture        : register( t5 );
Texture2D g_CookTorranceCompEAvgTexture     : register( t6 );
Texture2D g_CookTorranceCompInvCDFTexture   : register( t7 );
Texture2D g_CookTorranceCompPdfScaleTexture : register( t8 );
Texture2DArray g_CookTorranceCompEFresnelTexture : register( t9 );

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

float EvaluateCookTorranceCompEFresnel( float cosTheta, float alpha, float ior )
{
    float3 texelPos = float3( cosTheta, alpha, ( ior - 1.0f ) / 2.0f ) * ( g_CompEFresnelTextureSize.xyz - 1.0f );
    float3 uvw = ( texelPos + 0.5f ) * g_CompEFresnelTextureSizeRcp.xyz;
    float3 fraction = frac( texelPos );
    float4 values0 = g_CookTorranceCompEFresnelTexture.Gather( UVClampSampler, float3( uvw.xy, ( float ) ( int ) texelPos.z ) );
    float4 values1 = g_CookTorranceCompEFresnelTexture.Gather( UVClampSampler, float3( uvw.xy, ( float ) ( int ) texelPos.z + 1 ) );
    float2 value0 = lerp( values0.wx, values0.zy, fraction.x );
    float2 value1 = lerp( values1.wx, values1.zy, fraction.x );
    return lerp( lerp( value0.x, value0.y, fraction.y ), lerp( value1.x, value1.y, fraction.y ), fraction.z );
}

float3 CookTorranceCompSampleHemisphere( float2 sample, float alpha )
{
    //return ConsineSampleHemisphere(sample);
    float cosTheta = EvaluateCookTorranceCompInvCDF( sample.x, alpha );
    float phi = 2.0f * PI * sample.y;
    float s = sqrt( 1 - cosTheta * cosTheta );
    return float3( cos( phi ) * s, sin( phi ) * s, cosTheta );
}

float FresnelAverage( float ior )
{
    return ( ior - 1.0f ) / ( 4.08567f + 1.00071f * ior );
}

float EvaluateCookTorranceCompFresnel( float ior, float EAvg )
{
    //float4 f0 = EvaluateFresnelF0( ior );
    //float  f02 = f0 * f0;
    //float  f03 = f02 * f0;
    //return 0.04f * f0 + 0.66f * f02 + 0.3f * f03;

    float FAvg = FresnelAverage( ior );
    return FAvg * FAvg * EAvg / ( 1.0f - FAvg * ( 1.0f - EAvg ) );
}

float3 EvaluateCookTorranceCompBRDF( float3 wi, float3 wo, float3 reflectance, float alpha, float ior )
{
    float WIdotN = wi.z;
    float WOdotN = wo.z;
    if ( WIdotN <= 0.0f || WOdotN <= 0.0f )
        return 0.0f;

    float eI = EvaluateCookTorranceCompE( WIdotN, alpha );
    float eO = EvaluateCookTorranceCompE( WOdotN, alpha );
    float eAvg = EvaluateCookTorranceCompEAvg( alpha );
    float3 fresnel = EvaluateCookTorranceCompFresnel( ior, eAvg );
    return reflectance * ( 1.0f - eI ) * ( 1.0f - eO ) * fresnel / ( PI * ( 1.0f - eAvg ) );
}

float EvaluateCookTorranceCompPdf( float3 wi, float alpha )
{
    //return EvaluateLambertBRDFPdf(wi);
    float cosTheta = wi.z;
    if ( cosTheta < 0.0f )
        return 0.0f;

    float pdfScale = EvaluateCookTorranceCompPdfScale( alpha );
    return ( 1.0f - EvaluateCookTorranceCompE( cosTheta, alpha ) ) * cosTheta / pdfScale;
}

void SampleCookTorranceCompBRDF( float3 wo, float2 sample, float3 reflectance, float alpha, float ior, out float3 wi, out float3 value, out float pdf )
{
    wi = CookTorranceCompSampleHemisphere( sample, alpha );
    value = EvaluateCookTorranceCompBRDF( wi, wo, reflectance, alpha, ior );
    pdf = EvaluateCookTorranceCompPdf( wi, alpha );
}

#endif