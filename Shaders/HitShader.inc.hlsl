#ifndef _HITSHADER_H_
#define _HITSHADER_H_

#include "Intersection.inc.hlsl"

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
    , bool backface
    , StructuredBuffer<uint> materialIds
    , StructuredBuffer<Material> materials
    , inout Intersection intersection )
{
    intersection.position   = VectorBaryCentric3( v0.position, v1.position, v2.position, u, v );
    intersection.normal     = normalize( VectorBaryCentric3( v0.normal, v1.normal, v2.normal, u, v ) );
    intersection.tangent    = VectorBaryCentric3( v0.tangent, v1.tangent, v2.tangent, u, v );
    intersection.tangent    = normalize( intersection.tangent - dot( intersection.tangent, intersection.normal ) * intersection.normal ); // Orthogonalize tangent after interpolation

    float3 v0v1 = v1.position - v0.position;
    float3 v0v2 = v2.position - v0.position;
    intersection.geometryNormal = normalize( cross( v0v2, v0v1 ) );

    uint materialId = materialIds[ triangleId ];

    float2 texcoord = VectorBaryCentric2( v0.texcoord, v1.texcoord, v2.texcoord, u, v );
           texcoord *= materials[ materialId ].texTiling;

    float checkerboard = CheckerboardTexture( texcoord );

    uint materialFlags = materials[ materialId ].flags;
    float3 albedo = materials[ materialId ].albedo;
           albedo *= ( materialFlags & MATERIAL_FLAG_ALBEDO_TEXTURE ) != 0 ? checkerboard : 1.0f;
    float  roughness = materials[ materialId ].roughness;
           roughness *= ( materialFlags & MATERIAL_FLAG_ROUGHNESS_TEXTURE ) != 0 ? checkerboard : 1.0f;

    intersection.albedo     = albedo;
    intersection.alpha      = roughness * roughness;
    intersection.ior        = materials[ materialId ].ior;
    intersection.transmission = materials[ materialId ].transmission;
    intersection.isMetal    = ( materialFlags & MATERIAL_FLAG_IS_METAL ) != 0;
    intersection.isTwoSided = ( materialFlags & MATERIAL_FLAG_IS_TWOSIDED ) != 0;

    intersection.backface   = backface;
}

#endif