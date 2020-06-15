#ifndef _PRIMITIVES_H_
#define _PRIMITIVES_H_

struct Vertex
{
    float3  position;
    float3  normal;
    float3  tangent;
};

struct Intersection
{
    float3  albedo;
    float3  specular;
    float3  emission;
    float   alpha;
    float3  position;
    float3  normal;
    float3  tangent;
    float   rayEpsilon;
    float   ior;
};

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

bool RayTriangleIntersect( float3 origin
    , float3 direction
    , float tMin
    , float tMax
    , Vertex v0
    , Vertex v1
    , Vertex v2
    , out float t
    , out Intersection intersection )
{
    bool intersect = false;
    float u, v;
    if ( intersect = RayTriangleIntersect( origin, direction, tMin, tMax, v0, v1, v2, t, u, v ) )
    {
        intersection.position   = origin + t * direction;
        intersection.normal     = normalize( VectorBaryCentric( v0.normal, v1.normal, v2.normal, u, v ) );
        intersection.tangent    = normalize( VectorBaryCentric( v0.tangent, v1.tangent, v2.tangent, u, v ) );
        intersection.rayEpsilon = 1e-5f * t;

        intersection.albedo     = 1.0f;
        intersection.specular   = 1.0f;
        intersection.emission   = 0.0f;
        intersection.alpha      = 1.0f;
        intersection.ior        = 1.5f;
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