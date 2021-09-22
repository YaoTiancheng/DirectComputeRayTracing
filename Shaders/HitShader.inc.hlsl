#ifndef _HITSHADER_H_
#define _HITSHADER_H_

#include "Intersection.inc.hlsl"

float2 VectorBaryCentric2( float2 p0, float2 p1, float2 p2, float u, float v )
{
    float2 r1 = p1 - p0;
    float2 r2 = p2 - p0;
    r1 = r1 * u;
    r2 = r2 * v;
    r1 = r1 + p0;
    r1 = r1 + r2;
    return r1;
}

float3 VectorBaryCentric3( float3 p0, float3 p1, float3 p2, float u, float v )
{
    float3 r1 = p1 - p0;
    float3 r2 = p2 - p0;
    r1 = r1 * u;
    r2 = r2 * v;
    r1 = r1 + p0;
    r1 = r1 + r2;
    return r1;
}

float CheckerboardTexture( float2 texcoord )
{
    return ( ( uint( texcoord.x * 2 ) + uint( texcoord.y * 2 ) ) & 0x1 ) != 0 ? 1.0f : 0.0f;
}

void HitShader( float3 rayOrigin
    , float3 rayDirection
    , Vertex v0
    , Vertex v1
    , Vertex v2
    , float t
    , float u
    , float v
    , uint triangleId
    , float3 geometryNormal
    , bool backface
    , out Intersection intersection )
{
    intersection.position   = VectorBaryCentric3( v0.position, v1.position, v2.position, u, v );
    intersection.normal     = normalize( VectorBaryCentric3( v0.normal, v1.normal, v2.normal, u, v ) );
    intersection.tangent    = VectorBaryCentric3( v0.tangent, v1.tangent, v2.tangent, u, v );
    intersection.tangent    = normalize( intersection.tangent - dot( intersection.tangent, intersection.normal ) * intersection.normal ); // Orthogonalize tangent after interpolation
    intersection.geometryNormal = geometryNormal;

    uint materialId = g_MaterialIds[ triangleId ];

    float2 texcoord = VectorBaryCentric2( v0.texcoord, v1.texcoord, v2.texcoord, u, v );
           texcoord *= g_Materials[ materialId ].texTiling;

    float checkerboard = CheckerboardTexture( texcoord );

    uint materialFlags = g_Materials[ materialId ].flags;
    float3 albedo = g_Materials[ materialId ].albedo;
           albedo *= ( materialFlags & MATERIAL_FLAG_ALBEDO_TEXTURE ) != 0 ? checkerboard : 1.0f;
    float  roughness = g_Materials[ materialId ].roughness;
           roughness *= ( materialFlags & MATERIAL_FLAG_ROUGHNESS_TEXTURE ) != 0 ? checkerboard : 1.0f;
    float3 emission = g_Materials[ materialId ].emission;
           emission *= ( materialFlags & MATERIAL_FLAG_EMISSION_TEXTURE ) != 0 ? checkerboard : 1.0f;

    intersection.albedo     = albedo;
    intersection.specular   = 1.0f;
    intersection.emission   = emission;
    intersection.alpha      = roughness * roughness;
    intersection.ior        = g_Materials[ materialId ].ior;
    intersection.transmission = g_Materials[ materialId ].transmission;
    intersection.k          = g_Materials[ materialId ].k;
    intersection.isMetal    = ( materialFlags & MATERIAL_FLAG_IS_METAL ) != 0;

    intersection.backface   = backface;
}

#endif