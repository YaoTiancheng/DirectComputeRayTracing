#ifndef _HITSHADER_H_
#define _HITSHADER_H_

#include "Intersection.inc.hlsl"

float3 VectorBaryCentric( float3 p0, float3 p1, float3 p2, float u, float v )
{
    float3 r1 = p1 - p0;
    float3 r2 = p2 - p0;
    r1 = r1 * u;
    r2 = r2 * v;
    r1 = r1 + p0;
    r1 = r1 + r2;
    return r1;
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