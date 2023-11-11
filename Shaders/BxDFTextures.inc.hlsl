#ifndef _BXDF_TEXTURES_H_
#define _BXDF_TEXTURES_H_

#include "BxDFTextureDef.inc.hlsl"

// Remap texcoord 0 and 1 to center of the texel
float TexcoordRemap( uint dim, float u )
{
    return u * ( float( dim - 1 ) / dim ) + 0.5f / dim;
}

// Remap texcoord 0 and 1 to center of the texel
float2 TexcoordRemap( uint2 dim, float2 uv )
{
    return uv * ( float2( dim - 1 ) / dim ) + 0.5f / dim;
}

float SampleTexture1DLinear( Texture2D<float> tex, float u, uint dim )
{
    return tex.SampleLevel( UVClampSampler, TexcoordRemap( dim, u ), 0 );
}

float SampleTexture2DLinear( Texture2D<float> tex, float2 uv, uint2 dim )
{
    return tex.SampleLevel( UVClampSampler, TexcoordRemap( dim, uv ), 0 );
}

float SampleTextureArrayLinear( Texture2DArray<float> tex, float3 uvw, uint3 dim, uint sliceOffset )
{
    float slicePos = uvw.z * ( dim.z - 1.0f );
    float fraction = frac( slicePos );
    float value0 = tex.SampleLevel( UVClampSampler, float3( TexcoordRemap( dim.xy, uvw.xy ), (float)( (int)slicePos + sliceOffset ) ), 0 );
    float value1 = tex.SampleLevel( UVClampSampler, float3( TexcoordRemap( dim.xy, uvw.xy ), (float)( (int)slicePos + 1 + sliceOffset ) ), 0 );
    return lerp( value0, value1, fraction );
} 

#define BXDFTEX_COOKTORRANCE_E_SIZE                     float2( BXDFTEX_COOKTORRANCE_E_SIZE_X, BXDFTEX_COOKTORRANCE_E_SIZE_Y )
#define BXDFTEX_COOKTORRANCE_E_FRESNEL_SIZE             float3( BXDFTEX_COOKTORRANCE_E_FRESNEL_DIELECTRIC_SIZE_X, BXDFTEX_COOKTORRANCE_E_FRESNEL_DIELECTRIC_SIZE_Y, BXDFTEX_COOKTORRANCE_E_FRESNEL_DIELECTRIC_SIZE_Z )
#define BXDFTEX_COOKTORRANCE_BSDF_E_AVG_SIZE            float3( BXDFTEX_COOKTORRANCE_E_FRESNEL_DIELECTRIC_SIZE_Y, BXDFTEX_COOKTORRANCE_E_FRESNEL_DIELECTRIC_SIZE_Z, 1 )
#define BXDFTEX_COOKTORRANCE_BSDF_PDF_SCALE_SIZE        float3( BXDFTEX_COOKTORRANCE_E_FRESNEL_DIELECTRIC_SIZE_Y, BXDFTEX_COOKTORRANCE_E_FRESNEL_DIELECTRIC_SIZE_Z, 1 )
#define BXDFTEX_COOKTORRANCE_BRDF_INV_CDF_SIZE          float2( BXDFTEX_COOKTORRANCE_MULTISCATTERING_INV_CDF_X, BXDFTEX_COOKTORRANCE_MULTISCATTERING_INV_CDF_Y )
#define BXDFTEX_COOKTORRANCE_BRDF_PDF_SCALE_SIZE        BXDFTEX_COOKTORRANCE_MULTISCATTERING_PDF_SCALE_X


float SampleCookTorranceMicrofacetBRDFEnergyTexture( float cosThetaO, float alpha )
{
    float2 uv = float2( cosThetaO, alpha );
    return SampleTexture2DLinear( g_CookTorranceCompETexture, uv, BXDFTEX_COOKTORRANCE_E_SIZE );
}

float SampleCookTorranceMicrofacetBRDFAverageEnergyTexture( float alpha )
{
    return SampleTexture1DLinear( g_CookTorranceCompEAvgTexture, alpha, BXDFTEX_COOKTORRANCE_E_AVG_SIZE_X );
}

float SampleCookTorranceMicrofacetBRDFEnergyFresnelDielectricTexture( float cosThetaO, float alpha, float eta )
{
    bool inverseEta = eta < 1.0f;
    eta = inverseEta ? 1.0f / eta : eta;
    uint sliceOffset = inverseEta ? BXDFTEX_COOKTORRANCE_E_FRESNEL_DIELECTRIC_SIZE_Z : 0;
    float w = ( eta - 1.0f ) / 2.0f;
    float3 uvw = float3( cosThetaO, alpha, w );
    return SampleTextureArrayLinear( g_CookTorranceCompEFresnelTexture, uvw, BXDFTEX_COOKTORRANCE_E_FRESNEL_SIZE, sliceOffset );
}

float SampleCookTorranceMicrofacetBSDFEnergyTexture( float cosThetaO, float alpha, float eta )
{
    bool inverseEta = eta < 1.0f;
    eta = inverseEta ? 1.0f / eta : eta;
    uint sliceOffset = inverseEta ? BXDFTEX_COOKTORRANCE_E_FRESNEL_DIELECTRIC_SIZE_Z : 0;
    float w = ( eta - 1.0f ) / 2.0f;
    float3 uvw = float3( cosThetaO, alpha, w );
    return SampleTextureArrayLinear( g_CookTorranceBSDFETexture, uvw, BXDFTEX_COOKTORRANCE_E_FRESNEL_SIZE, sliceOffset );
}

float SampleCookTorranceMicrofacetBSDFAverageEnergyTexture( float alpha, float eta )
{
    bool inverseEta = eta < 1.0f;
    eta = inverseEta ? 1.0f / eta : eta;
    uint sliceOffset = inverseEta ? 1 : 0;
    float v = ( eta - 1.0f ) / 2.0f;
    float3 uvw = float3( alpha, v, 0.0f );
    return SampleTextureArrayLinear( g_CookTorranceBSDFAvgETexture, uvw, BXDFTEX_COOKTORRANCE_BSDF_E_AVG_SIZE, sliceOffset );
}

#endif