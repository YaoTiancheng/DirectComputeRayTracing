#ifndef _PRIMITIVES_H_
#define _PRIMITIVES_H_

#include "Intersection.inc.hlsl"
#include "Vertex.inc.hlsl"

bool RayTriangleIntersect( float3 origin
    , float3 direction
    , float tMin
    , float tMax
    , float3 v0
    , float3 v1
    , float3 v2
    , out float t
    , out float u
    , out float v
    , out bool backface )
{
    float3 v0v1 = v1 - v0; 
    float3 v0v2 = v2 - v0;

    float3 pvec = cross( direction, v0v2 );
    float det = dot( v0v1, pvec );

    float invDet = 1 / det; 
 
    float3 tvec = origin - v0;
    u = dot( tvec, pvec ) * invDet; 

    float3 qvec = cross( tvec, v0v1 );
    v = dot( direction, qvec ) * invDet; 

    t = dot( v0v2, qvec ) * invDet; 

    backface = det > -1e-10;

    return abs( det ) >= 1e-10 && u >= 0 && u <= 1 && v >= 0 && u + v <= 1 && t >= tMin && t < tMax;
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

    return t1 >= t0 && ( t0 < tMax && t1 >= tMin );
}

#endif