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

#endif