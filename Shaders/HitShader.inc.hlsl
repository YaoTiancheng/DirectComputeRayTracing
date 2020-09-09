#ifndef _HITSHADER_H_
#define _HITSHADER_H_

#include "Primitives.inc.hlsl"
#include "Intersection.inc.hlsl"

void HitShader( float3 rayOrigin
    , float3 rayDirection
    , Vertex v0
    , Vertex v1
    , Vertex v2
    , float t
    , float u
    , float v
    , uint triangleId
    , out Intersection intersection )
{
    intersection.position   = rayOrigin + t * rayDirection;
    intersection.normal     = normalize( VectorBaryCentric( v0.normal, v1.normal, v2.normal, u, v ) );
    intersection.tangent    = normalize( VectorBaryCentric( v0.tangent, v1.tangent, v2.tangent, u, v ) );
    intersection.rayEpsilon = 1e-5f * t;

    intersection.albedo     = g_Materials[ g_MaterialIds[ triangleId ] ].albedo;
    intersection.specular   = 1.0f;
    intersection.emission   = g_Materials[ g_MaterialIds[ triangleId ] ].emission;
    intersection.alpha      = g_Materials[ g_MaterialIds[ triangleId ] ].roughness;
    intersection.ior        = g_Materials[ g_MaterialIds[ triangleId ] ].ior;
}

#endif