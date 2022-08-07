#ifndef _LIGHT_H_
#define _LIGHT_H_

#include "RayPrimitiveIntersect.inc.hlsl"

void SampleLight_Point( SLight light, float2 samples, float3 position, out float3 radiance, out float3 wi, out float distance, out float pdf )
{
    float3 lightPosWS = light.transform._41_42_43;
    wi = lightPosWS - position;
    distance = length( wi );
    wi /= distance;
    radiance = light.color / ( distance * distance );
    pdf = 1.0f;
}

void EvaluateLight_Rectangle( SLight light, float3 wi, float3 position, out float3 radiance, out float pdf, out float distance )
{
    float3 normalWS = light.transform._31_32_33;
    float3 lightPosWS = light.transform._41_42_43;
    float2 size = float2( length( light.transform._11_12_13 ), length( light.transform._21_22_23 ) ); // This matrix shouldn't contain shear or negative scaling, otherwise this is wrong!

    float WIdotN = -dot( wi, normalWS );
    float hitPlaneDistance = dot( lightPosWS - position, normalWS ) / ( -WIdotN ); // Ray-plane intersection
    float3 hitPosWS = position + wi * hitPlaneDistance;
    float3 hitPosVec = hitPosWS - lightPosWS;
    float2 hitPosLS = float2( dot( hitPosVec, light.transform._11_12_13 ), dot( hitPosVec, light.transform._21_22_23 ) );
           hitPosLS /= size * size;
    float area = size.x * size.y;

    bool hasHit = WIdotN > 0.0f && hitPlaneDistance > 0.0f && all( abs( hitPosLS ) <= 0.5f );
    pdf = hasHit ? ( hitPlaneDistance * hitPlaneDistance ) / ( WIdotN * area ) : 0.0f;
    radiance = hasHit ? light.color : 0.0f;
    distance = hasHit ? hitPlaneDistance : FLT_INF;
}

void SampleLight_Rectangle( SLight light, float2 samples, float3 position, out float3 radiance, out float3 wi, out float distance, out float pdf )
{
    float3 normalWS = light.transform._31_32_33;

    float2 samplePosLS = samples - 0.5f; // Map to [-0.5, 0.5]
    float3 samplePosWS = mul( float4( samplePosLS, 0.0f, 1.0f ), light.transform ).xyz;
    wi = samplePosWS - position;
    distance = length( wi );
    wi /= distance;

    float WIdotN = -dot( wi, normalWS );
    radiance = WIdotN > 0.0f ? light.color : 0.0f;

    float2 size = float2( length( light.transform._11_12_13 ), length( light.transform._21_22_23 ) ); // This matrix shouldn't contain shear or negative scaling, otherwise this is wrong!
    float area = size.x * size.y;
    pdf = WIdotN > 0.0f ? ( distance * distance ) / ( WIdotN * area ) : 0.0f;
}

void EvaluateLight_Triangle( SLight light, float3 v0, float3 v1, float3 v2, float3 wi, float3 position, out float3 radiance, out float pdf, out float distance )
{
    float u, v;
    bool backface;
    bool hasHit = RayTriangleIntersect( position, wi, 0.f, FLT_INF, v0, v1, v2, distance, u, v, backface );
    float3 v0v1 = v1 - v0;
    float3 v0v2 = v2 - v0;
    float3 normalWS = normalize( cross( v0v2, v0v1 ) );
    float WIdotN = -dot( wi, normalWS );
    float invSurfaceArea = light.invSurfaceArea;
    pdf = hasHit && WIdotN > 0.0f ? ( distance * distance * invSurfaceArea ) / WIdotN : 0.f;
    radiance = hasHit && WIdotN > 0.0f ? light.color : 0.f;
    distance = distance;
}

void SampleLight_Triangle( SLight light, float3 v0, float3 v1, float3 v2, float2 samples, float3 position, out float3 radiance, out float3 wi, out float distance, out float pdf )
{
    float2 samplePosBarycentric = SampleTriangle( samples );
    float3 samplePosWS = VectorBaryCentric3( v0, v1, v2, samplePosBarycentric.x, samplePosBarycentric.y );

    float3 v0v1 = v1 - v0;
    float3 v0v2 = v2 - v0;
    float3 crossProduct = cross( v0v2, v0v1 );
    float3 normalWS = normalize( crossProduct );

    wi = samplePosWS - position;
    distance = length( wi );
    wi /= distance;

    float WIdotN = -dot( wi, normalWS );
    radiance = WIdotN > 0.0f ? light.color : 0.0f;

    float invSurfaceArea = light.invSurfaceArea;
    pdf = WIdotN > 0.0f ? ( distance * distance * invSurfaceArea ) / WIdotN : 0.0f;
}

#endif