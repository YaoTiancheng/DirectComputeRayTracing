#ifndef _PRIMITIVES_H_
#define _PRIMITIVES_H_

#include "Intersection.inc.hlsl"
#include "Vertex.inc.hlsl"

bool RayTriangleIntersect( float3 origin
    , float3 direction
    , float tMin
    , float tMax
    , Vertex v0
    , Vertex v1
    , Vertex v2
    , out float t
    , out float u
    , out float v )
{
    float3 v0v1 = v1.position - v0.position; 
    float3 v0v2 = v2.position - v0.position;
    float3 pvec = cross( direction, v0v2 );
    float det = dot( v0v1, pvec );

    if ( abs( det ) < 0.00001f )
        return false; 

    float invDet = 1 / det; 
 
    float3 tvec = origin - v0.position;
    u = dot( tvec, pvec ) * invDet; 
    if ( u < 0 || u > 1 )
        return false; 
 
    float3 qvec = cross( tvec, v0v1 );
    v = dot( direction, qvec ) * invDet; 
    if ( v < 0 || u + v > 1 )
        return false; 
 
    t = dot( v0v2, qvec ) * invDet; 

    if ( t < tMin )
        return false;
    if ( t >= tMax )
        return false;
 
    return true; 
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
    , out Intersection intersection );

bool RayTriangleIntersect( float3 origin
    , float3 direction
    , float tMin
    , float tMax
    , Vertex v0
    , Vertex v1
    , Vertex v2
    , uint triangleId
    , out float t
    , out Intersection intersection )
{
    bool intersect = false;
    float u, v;
    if ( intersect = RayTriangleIntersect( origin, direction, tMin, tMax, v0, v1, v2, t, u, v ) )
    {
        HitShader( origin, direction, v0, v1, v2, t, u, v, triangleId, intersection );
    }
    return intersect;
}

bool RayAABBIntersect( float3 origin
    , float3 invDir
    , float tMin
    , float tMax
    , float3 bboxMin
    , float3 bboxMax
)
{
    float tx0 = ( bboxMin.x - origin.x ) * invDir.x;
    float tx1 = ( bboxMax.x - origin.x ) * invDir.x;

    float t0 = min( tx0, tx1 );
    float t1 = max( tx0, tx1 );

    float ty0 = ( bboxMin.y - origin.y ) * invDir.y;
    float ty1 = ( bboxMax.y - origin.y ) * invDir.y;

    t0 = max( t0, min( ty0, ty1 ) );
    t1 = min( t1, max( ty0, ty1 ) );

    float tz0 = ( bboxMin.z - origin.z ) * invDir.z;
    float tz1 = ( bboxMax.z - origin.z ) * invDir.z;

    t0 = max( t0, min( tz0, tz1 ) );
    t1 = min( t1, max( tz0, tz1 ) );

    return t1 >= t0 && ( tMin <= t0 || tMax > t1 );
}

#endif