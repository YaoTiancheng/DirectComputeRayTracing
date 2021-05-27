#ifndef _BXDF_TEXTURES_H_
#define _BXDF_TEXTURES_H_

#include "BxDFTextureDef.inc.hlsl"

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

SAMPLE_TEXTURE_ARRAY_LINEAR( g_CookTorranceCompEFresnelTexture )
SAMPLE_TEXTURE_ARRAY_LINEAR( g_CookTorranceBSDFEFresnelTexture )


#define BXDFTEX_COOKTORRANCE_E_FRESNEL_DIELECTRIC_SIZE  float3( BXDFTEX_COOKTORRANCE_E_FRESNEL_DIELECTRIC_SIZE_X, BXDFTEX_COOKTORRANCE_E_FRESNEL_DIELECTRIC_SIZE_Y, BXDFTEX_COOKTORRANCE_E_FRESNEL_DIELECTRIC_SIZE_Z )

float SampleCookTorranceMicrofacetBRDFEnergyFresnelDielectricTexture( float cosThetaO, float alpha, float eta )
{
    bool inverseEta = eta < 1.0f;
    eta = inverseEta ? 1.0f / eta : eta;
    uint sliceOffset = inverseEta ? BXDFTEX_COOKTORRANCE_E_FRESNEL_DIELECTRIC_SIZE_Z : 0.0f;
    float w = ( eta - 1.0f ) / 2.0f;
    float3 uvw = float3( cosThetaO, alpha, w );
    return SampleTextureArrayLinear_g_CookTorranceCompEFresnelTexture( UVClampSampler, uvw, BXDFTEX_COOKTORRANCE_E_FRESNEL_DIELECTRIC_SIZE, sliceOffset );
}

float SampleCookTorranceMicrofacetBSDFEnergyFresnelDielectricTexture( float cosThetaO, float alpha, float eta )
{
    bool inverseEta = eta < 1.0f;
    eta = inverseEta ? 1.0f / eta : eta;
    uint sliceOffset = inverseEta ? BXDFTEX_COOKTORRANCE_E_FRESNEL_DIELECTRIC_SIZE_Z : 0.0f;
    float w = ( eta - 1.0f ) / 2.0f;
    float3 uvw = float3( cosThetaO, alpha, w );
    return SampleTextureArrayLinear_g_CookTorranceBSDFEFresnelTexture( UVClampSampler, uvw, BXDFTEX_COOKTORRANCE_E_FRESNEL_DIELECTRIC_SIZE, sliceOffset );
}

#endif