#ifndef _BXDF_TEXTURES_H_
#define _BXDF_TEXTURES_H_

#include "BxDFTextureDef.inc.hlsl"

#define SAMPLE_TEXTURE_1D_LINEAR( TEX ) \
float SampleTexture1DLinear_##TEX( SamplerState s, float u, float dim ) \
{ \
    float2 dim2 = float2( dim, 1 ); \
    float2 texelPos = float2( u, 0.0f ) * ( dim2 - 1.0f ); \
    float2 uv = ( texelPos + 0.5f ) / dim2; \
    float4 values = TEX.Gather( s, uv ); \
    float fraction = frac( texelPos.x ); \
    return lerp( values.w, values.z, fraction ); \
}

#define SAMPLE_TEXTURE_2D_LINEAR( TEX ) \
float SampleTexture2DLinear_##TEX( SamplerState s, float2 uv, float2 dim ) \
{ \
    float2 texelPos = uv * ( dim - 1.0f ); \
           uv = ( texelPos + 0.5f ) / dim; \
    float2 fraction = frac( texelPos ); \
    float4 values = TEX.Gather( s, uv ); \
    float2 value = lerp( values.wx, values.zy, fraction.x ); \
    return lerp( value.x, value.y, fraction.y ); \
}

#define SAMPLE_TEXTURE_ARRAY_LINEAR( TEX ) \
float SampleTextureArrayLinear_##TEX( SamplerState s, float3 uvw, float3 dim, uint sliceOffset ) \
{ \
    float3 texelPos = uvw * ( dim - 1.0f ); \
           uvw = ( texelPos + 0.5f ) / dim; \
    float3 fraction = frac( texelPos ); \
    float4 values0 = TEX.Gather( s, float3( uvw.xy, (float)( (int)texelPos.z + sliceOffset ) ) ); \
    float4 values1 = TEX.Gather( s, float3( uvw.xy, (float)( (int)texelPos.z + 1 + sliceOffset ) ) ); \
    float2 value0 = lerp( values0.wx, values0.zy, fraction.x ); \
    float2 value1 = lerp( values1.wx, values1.zy, fraction.x ); \
    return lerp( lerp( value0.x, value0.y, fraction.y ), lerp( value1.x, value1.y, fraction.y ), fraction.z ); \
} 

SAMPLE_TEXTURE_1D_LINEAR( g_CookTorranceCompEAvgTexture )
SAMPLE_TEXTURE_2D_LINEAR( g_CookTorranceCompETexture )
SAMPLE_TEXTURE_ARRAY_LINEAR( g_CookTorranceCompEFresnelTexture )
SAMPLE_TEXTURE_ARRAY_LINEAR( g_CookTorranceBSDFETexture )
SAMPLE_TEXTURE_ARRAY_LINEAR( g_CookTorranceBSDFAvgETexture )
SAMPLE_TEXTURE_ARRAY_LINEAR( g_CookTorranceBTDFETexture )


#define BXDFTEX_COOKTORRANCE_E_SIZE                     float2( BXDFTEX_COOKTORRANCE_E_SIZE_X, BXDFTEX_COOKTORRANCE_E_SIZE_Y )
#define BXDFTEX_COOKTORRANCE_E_FRESNEL_SIZE             float3( BXDFTEX_COOKTORRANCE_E_FRESNEL_DIELECTRIC_SIZE_X, BXDFTEX_COOKTORRANCE_E_FRESNEL_DIELECTRIC_SIZE_Y, BXDFTEX_COOKTORRANCE_E_FRESNEL_DIELECTRIC_SIZE_Z )
#define BXDFTEX_COOKTORRANCE_BSDF_E_AVG_SIZE            float3( BXDFTEX_COOKTORRANCE_E_FRESNEL_DIELECTRIC_SIZE_Y, BXDFTEX_COOKTORRANCE_E_FRESNEL_DIELECTRIC_SIZE_Z, 1 )


float SampleCookTorranceMicrofacetBRDFEnergyTexture( float cosThetaO, float alpha )
{
    float2 uv = float2( cosThetaO, alpha );
    return SampleTexture2DLinear_g_CookTorranceCompETexture( UVClampSampler, uv, BXDFTEX_COOKTORRANCE_E_SIZE );
}

float SampleCookTorranceMicrofacetBRDFAverageEnergyTexture( float alpha )
{
    return SampleTexture1DLinear_g_CookTorranceCompEAvgTexture( UVClampSampler, alpha, BXDFTEX_COOKTORRANCE_E_AVG_SIZE_X );
}

float SampleCookTorranceMicrofacetBRDFEnergyFresnelDielectricTexture( float cosThetaO, float alpha, float eta )
{
    bool inverseEta = eta < 1.0f;
    eta = inverseEta ? 1.0f / eta : eta;
    uint sliceOffset = inverseEta ? BXDFTEX_COOKTORRANCE_E_FRESNEL_DIELECTRIC_SIZE_Z : 0;
    float w = ( eta - 1.0f ) / 2.0f;
    float3 uvw = float3( cosThetaO, alpha, w );
    return SampleTextureArrayLinear_g_CookTorranceCompEFresnelTexture( UVClampSampler, uvw, BXDFTEX_COOKTORRANCE_E_FRESNEL_SIZE, sliceOffset );
}

float SampleCookTorranceMicrofacetBSDFEnergyTexture( float cosThetaO, float alpha, float eta )
{
    bool inverseEta = eta < 1.0f;
    eta = inverseEta ? 1.0f / eta : eta;
    uint sliceOffset = inverseEta ? BXDFTEX_COOKTORRANCE_E_FRESNEL_DIELECTRIC_SIZE_Z : 0;
    float w = ( eta - 1.0f ) / 2.0f;
    float3 uvw = float3( cosThetaO, alpha, w );
    return SampleTextureArrayLinear_g_CookTorranceBSDFETexture( UVClampSampler, uvw, BXDFTEX_COOKTORRANCE_E_FRESNEL_SIZE, sliceOffset );
}

float SampleCookTorranceMicrofacetBSDFAverageEnergyTexture( float alpha, float eta )
{
    bool inverseEta = eta < 1.0f;
    eta = inverseEta ? 1.0f / eta : eta;
    uint sliceOffset = inverseEta ? 1 : 0;
    float v = ( eta - 1.0f ) / 2.0f;
    float3 uvw = float3( alpha, v, 0.0f );
    return SampleTextureArrayLinear_g_CookTorranceBSDFAvgETexture( UVClampSampler, uvw, BXDFTEX_COOKTORRANCE_BSDF_E_AVG_SIZE, sliceOffset );
}

float SampleCookTorranceMicrofacetBTDFEnergyTexture( float cosThetaO, float alpha, float eta )
{
    bool inverseEta = eta < 1.0f;
    eta = inverseEta ? 1.0f / eta : eta;
    uint sliceOffset = inverseEta ? BXDFTEX_COOKTORRANCE_E_FRESNEL_DIELECTRIC_SIZE_Z : 0;
    float w = ( eta - 1.0f ) / 2.0f;
    float3 uvw = float3( cosThetaO, alpha, w );
    return SampleTextureArrayLinear_g_CookTorranceBTDFETexture( UVClampSampler, uvw, BXDFTEX_COOKTORRANCE_E_FRESNEL_SIZE, sliceOffset );
}

#endif