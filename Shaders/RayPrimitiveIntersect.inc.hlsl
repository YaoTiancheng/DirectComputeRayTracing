#ifndef _PRIMITIVES_H_
#define _PRIMITIVES_H_

#include "Intersection.inc.hlsl"
#include "Vertex.inc.hlsl"

#if defined( WATERTIGHT_RAY_TRIANGLE_INTERSECTION )
bool RayTriangleIntersect( float3 origin // not permuted
    , float3 shearing // already permuted
    , uint3 permute
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
    t = 0.f;
    u = 0.f;
    v = 0.f;
    backface = false;

    float3 v0v1 = v1 - v0; 
    float3 v0v2 = v2 - v0;
    float3 crossProduct = cross( v0v1, v0v2 );
    if ( dot( crossProduct, crossProduct ) == 0.f ) // length square of the cross product
    {
        // Always miss a degenerated triangle
        return false;
    }

    // Translate vertices based on ray origin
    float3 p0t = PermuteFloat3( v0 - origin, permute );
    float3 p1t = PermuteFloat3( v1 - origin, permute );
    float3 p2t = PermuteFloat3( v2 - origin, permute );
    
    // Apply shear transformation to translated vertex positions
    p0t.xy += shearing.xy * p0t.z;
    p1t.xy += shearing.xy * p1t.z;
    p2t.xy += shearing.xy * p2t.z;

    float e0 = p1t.x * p2t.y - p2t.x * p1t.y;
    float e1 = p2t.x * p0t.y - p0t.x * p2t.y;
    float e2 = p0t.x * p1t.y - p1t.x * p0t.y;

    if ( ( e0 < 0.f || e1 < 0.f || e2 < 0.f ) && ( e0 > 0.f || e1 > 0.f || e2 > 0.f ) )
    {
        // Miss
        return false;
    }

    float det = e0 + e1 + e2;

    // Interpolate tScaled from vertex z
    p0t.z *= shearing.z;
    p1t.z *= shearing.z;
    p2t.z *= shearing.z;
    float tScaled = e0 * p0t.z + e1 * p1t.z + e2 * p2t.z;

    float invDet = 1.f / det;
    t = tScaled * invDet;
    u = e1 * invDet;
    v = e2 * invDet;
    backface = ( sign( shearing.z ) * det ) < 0.f; // backface is clock-wise
    
    return det != 0.f && t >= tMin && t < tMax;
}
#else
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
#endif

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