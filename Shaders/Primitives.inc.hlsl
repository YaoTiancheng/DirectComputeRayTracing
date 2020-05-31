#ifndef _PRIMITIVES_H_
#define _PRIMITIVES_H_

struct Sphere
{
    float4  position;
    float   radius;
    float4  albedo;
    float   metallic;
    float4  emission;
};

struct Vertex
{
    float4  position;
    float4  normal;
    float4  tangent;
};

struct Intersection
{
    float4  albedo;
    float4  specular;
    float4  emission;
    float   alpha;
    float4  position;
    float4  normal;
    float4  tangent;
    float   rayEpsilon;
    float   ior;
};

bool RaySphereIntersect( float4 origin
    , float4 direction
    , Sphere sphere
    , out float t )
{
    float radius2 = sphere.radius * sphere.radius;
    float t0, t1;
    float4 l = sphere.position - origin;
    float tca = dot( l, direction );
    float d2 = dot( l, l ) - tca * tca;
    if ( d2 >= radius2 )
        return false;
    float thc = sqrt( radius2 - d2 );
    t0 = tca - thc;
    t1 = tca + thc;

    if ( t0 < 0 )
    {
        t0 = t1;
        if ( t0 < 0 )
            return false;
    }
    t = t0;
    return true;
}

bool RaySphereIntersect( float4 origin
    , float4 direction
    , Sphere sphere
    , out float t
    , out Intersection intersection )
{
    bool intersect = false;
    if ( intersect = RaySphereIntersect( origin, direction, sphere, t ) )
    {
        intersection.position = origin + t * direction;
        intersection.normal = normalize( intersection.position - sphere.position );
        intersection.tangent = normalize( float4( cross( float3( 0.0f, 1.0f, 0.0f ), intersection.normal.xyz ), 0.0f ) );
        if ( isinf( intersection.tangent.x ) )
            intersection.tangent = float4( 1.0f, 0.0f, 0.0f, 0.0f );
        intersection.rayEpsilon = 1e-3f * t;

        intersection.albedo = float4( lerp( sphere.albedo.rgb, 0.0f, sphere.metallic ), sphere.albedo.a );
        intersection.specular = lerp( 1.0f, sphere.albedo, sphere.metallic );
        intersection.emission = sphere.emission;
        intersection.alpha = 0.3f;
        intersection.ior = 1.8f /*lerp(1.5f, 0.0f, sphere.metallic)*/;
    }
    return intersect;
}

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

bool RayTriangleIntersect( float4 origin
    , float4 direction
    , Vertex v0
    , Vertex v1
    , Vertex v2
    , out float t
    , out float u
    , out float v )
{
    float3 v0v1 = v1.position.xyz - v0.position.xyz; 
    float3 v0v2 = v2.position.xyz - v0.position.xyz; 
    float3 pvec = cross( direction.xyz, v0v2 );
    float det = dot( v0v1, pvec );

    if ( abs( det ) < 0.00001f )
        return false; 

    float invDet = 1 / det; 
 
    float3 tvec = origin.xyz - v0.position.xyz; 
    u = dot( tvec, pvec ) * invDet; 
    if ( u < 0 || u > 1 )
        return false; 
 
    float3 qvec = cross( tvec, v0v1 );
    v = dot( direction.xyz, qvec ) * invDet; 
    if ( v < 0 || u + v > 1 )
        return false; 
 
    t = dot( v0v2, qvec ) * invDet; 

    if ( t < 0.0f )
        return false;
 
    return true; 
}

bool RayTriangleIntersect( float4 origin
    , float4 direction
    , Vertex v0
    , Vertex v1
    , Vertex v2
    , out float t
    , out Intersection intersection )
{
    bool intersect = false;
    float u, v;
    if ( intersect = RayTriangleIntersect( origin, direction, v0, v1, v2, t, u, v ) )
    {
        intersection.position   = origin + t * direction;
        intersection.normal     = float4( normalize( VectorBaryCentric( v0.normal.xyz, v1.normal.xyz, v2.normal.xyz, u, v ) ), 0.0f );
        intersection.tangent    = float4( normalize( VectorBaryCentric( v0.tangent.xyz, v1.tangent.xyz, v2.tangent.xyz, u, v ) ), 0.0f );
        intersection.rayEpsilon = 1e-4f * t;

        intersection.albedo     = float4( 1.0f, 1.0f, 1.0f, 1.0f );
        intersection.specular   = 1.0f;
        intersection.emission   = 0.0f;
        intersection.alpha      = 0.2f;
        intersection.ior        = 1.8f /*lerp(1.5f, 0.0f, sphere.metallic)*/;
    }
    return intersect;
}

#endif