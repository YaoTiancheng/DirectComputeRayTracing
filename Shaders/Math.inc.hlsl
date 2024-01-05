#ifndef _MATH_H_
#define _MATH_H_

static const float PI = 3.14159265359;
static const float PI_MUL_2 = 6.283185307;
static const float INV_PI = 1 / PI;

float SafeSqrt( float value )
{
    return sqrt( max( 0, value ) );
}

float2 SafeSqrt( float2 value )
{
    return sqrt( max( 0, value ) );
}

float3 SafeSqrt( float3 value )
{
    return sqrt( max( 0, value ) );
}

float2 VectorBaryCentric2( float2 p0, float2 p1, float2 p2, float u, float v )
{
    float2 r1 = p1 - p0;
    float2 r2 = p2 - p0;
    r1 = r1 * u;
    r2 = r2 * v;
    r1 = r1 + p0;
    r1 = r1 + r2;
    return r1;
}

float3 VectorBaryCentric3( float3 p0, float3 p1, float3 p2, float u, float v )
{
    float3 r1 = p1 - p0;
    float3 r2 = p2 - p0;
    r1 = r1 * u;
    r2 = r2 * v;
    r1 = r1 + p0;
    r1 = r1 + r2;
    return r1;
}

#endif