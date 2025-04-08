#ifndef _HITSHADER_H_
#define _HITSHADER_H_

#include "Intersection.inc.hlsl"

#define DEGEN_TANGENT_LENGTH_THRESHOLD 0.000001f

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
    , Texture2D<float4> textures[]
    , SamplerState samplerState
    , inout Intersection intersection )
{
    intersection.position   = VectorBaryCentric3( v0.position, v1.position, v2.position, u, v );
    intersection.normal     = normalize( VectorBaryCentric3( v0.normal, v1.normal, v2.normal, u, v ) );

    // Lengthy tangent calculation to make sure the hit shader always outputs a valid tangent which is 1) not degenerated and 2) orthogonal to the normal
    float3 tangent = VectorBaryCentric3( v0.tangent, v1.tangent, v2.tangent, u, v );
    float tangentLength = length( tangent );
    // Orthonormalize if the interpolated tangent is not degenerated
    if ( tangentLength >= DEGEN_TANGENT_LENGTH_THRESHOLD )
    {
        tangent = tangent - dot( tangent, intersection.normal ) * intersection.normal; 
        tangentLength = length( tangent );
    }
    // Pick a new tangent on the xz plane if the interpolated or the orthonormalized tangent is degenerated.
    // Note: orthonormalized tangent could be degenerated when the normal is parallel to the interpolated tangent.
    if ( tangentLength < DEGEN_TANGENT_LENGTH_THRESHOLD )
    {
        tangent = cross( intersection.normal, float3( 0.f, 1.f, 0.f ) );
        tangentLength = length( tangent );
        tangent = tangentLength >= DEGEN_TANGENT_LENGTH_THRESHOLD ? tangent : float3( 1.f, 0.f, 0.f ); // If the normal is up, any vector on xz plane works so (1,0,0) is used.
    }
    intersection.tangent = tangent / tangentLength; // normalize the tangent
    
    float3 v0v1 = v1.position - v0.position;
    float3 v0v2 = v2.position - v0.position;
    intersection.geometryNormal = normalize( cross( v0v2, v0v1 ) );

    uint materialId = materialIds[ triangleId ];

    float2 texcoord = VectorBaryCentric2( v0.texcoord, v1.texcoord, v2.texcoord, u, v );
           texcoord *= materials[ materialId ].texTiling;

    float3 albedo = materials[ materialId ].albedo;
    int albedoTextureIndex = materials[ materialId ].albedoTextureIndex;
    [branch]
    if ( albedoTextureIndex != -1 )
    {
        albedo *= textures[ NonUniformResourceIndex( albedoTextureIndex ) ].SampleLevel( samplerState, texcoord, 0 ).rgb;
    }

    float checkerboard = CheckerboardTexture( texcoord );
    uint materialFlags = materials[ materialId ].flags;
    float  roughness = materials[ materialId ].roughness;
           roughness *= ( materialFlags & MATERIAL_FLAG_ROUGHNESS_TEXTURE ) != 0 ? checkerboard : 1.0f;

    intersection.albedo     = albedo;
    intersection.alpha      = roughness * roughness;
    intersection.ior        = materials[ materialId ].ior;

    intersection.materialType = materialFlags & MATERIAL_FLAG_TYPE_MASK;
    intersection.isTwoSided = (materialFlags & MATERIAL_FLAG_IS_TWOSIDED) != 0;
    intersection.multiscattering = (materialFlags & MATERIAL_FLAG_MULTISCATTERING) != 0;
    intersection.backface = backface;
}

#endif