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

#define BXDFTEX_BRDF_SIZE                float2( BXDFTEX_BRDF_SIZE_X, BXDFTEX_BRDF_SIZE_Y )
#define BXDFTEX_BRDF_DIELECTRIC_SIZE     float3( BXDFTEX_BRDF_DIELECTRIC_SIZE_X, BXDFTEX_BRDF_DIELECTRIC_SIZE_Y, BXDFTEX_BRDF_DIELECTRIC_SIZE_Z )
#define BXDFTEX_BSDF_AVG_SIZE            float3( BXDFTEX_BRDF_DIELECTRIC_SIZE_Y, BXDFTEX_BRDF_DIELECTRIC_SIZE_Z, 1 )

float SampleBRDFTexture( float cosThetaO, float alpha )
{
    float2 uv = float2( cosThetaO, alpha );
    return SampleTexture2DLinear( g_BRDFTexture, uv, BXDFTEX_BRDF_SIZE );
}

float SampleBRDFAverageTexture( float alpha )
{
    return SampleTexture1DLinear( g_BRDFAvgTexture, alpha, BXDFTEX_BRDF_SIZE_Y );
}

float SampleBRDFDielectricTexture( float cosThetaO, float alpha, float eta, bool isEntering )
{
    uint sliceOffset = isEntering ? BXDFTEX_BRDF_DIELECTRIC_SIZE_Z : 0;
    float w = ( eta - 1.0f ) / 2.0f;
    float3 uvw = float3( cosThetaO, alpha, w );
    return SampleTextureArrayLinear( g_BRDFDielectricTexture, uvw, BXDFTEX_BRDF_DIELECTRIC_SIZE, sliceOffset );
}

float SampleBSDFTexture( float cosThetaO, float alpha, float eta, bool isEntering )
{
    uint sliceOffset = isEntering ? BXDFTEX_BRDF_DIELECTRIC_SIZE_Z : 0;
    float w = ( eta - 1.0f ) / 2.0f;
    float3 uvw = float3( cosThetaO, alpha, w );
    return SampleTextureArrayLinear( g_BSDFTexture, uvw, BXDFTEX_BRDF_DIELECTRIC_SIZE, sliceOffset );
}

float SampleBSDFAverageTexture( float alpha, float eta, bool isEntering )
{
    uint sliceOffset = isEntering ? 1 : 0;
    float v = ( eta - 1.0f ) / 2.0f;
    float3 uvw = float3( alpha, v, 0.0f );
    return SampleTextureArrayLinear( g_BSDFAvgTexture, uvw, BXDFTEX_BSDF_AVG_SIZE, sliceOffset );
}

#endif