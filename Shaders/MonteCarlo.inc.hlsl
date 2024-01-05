#ifndef _MONTECARLO_H_
#define _MONTECARLO_H_

#include "Math.inc.hlsl"

float2 ConcentricSampleDisk( float2 sample )
{
    float r, theta;
    float2 s = 2 * sample - 1;

    if ( s.x == 0.0f && s.y == 0.0f )
    {
        return ( float2 ) 0.0f;
    }
    if ( s.x >= -s.y )
    {
        if ( s.x > s.y )
        {
            r = s.x;
            if ( s.y > 0.0f )
                theta = s.y / r;
            else
                theta = 8.0f + s.y / r;
        }
        else
        {
            r = s.y;
            theta = 2.0f - s.x / r;
        }
    }
    else
    {
        if ( s.x <= s.y )
        {
            r = -s.x;
            theta = 4.0f - s.y / r;
        }
        else
        {
            r = -s.y;
            theta = 6.0f + s.x / r;
        }
    }

    theta *= PI / 4.0f;
    return r * float2( cos( theta ), sin( theta ) );
}

float3 ConsineSampleHemisphere( float2 sample )
{
    sample = ConcentricSampleDisk( sample );
    return float3( sample.xy, sqrt( max( 0.0f, 1.0f - dot( sample.xy, sample.xy ) ) ) );
}

// Returns barycentric coordinate
float2 SampleTriangle( float2 sample )
{
    float s = sqrt( sample.x );
    return float2( 1.0f - s, sample.y * s );
}

float3 SampleSphere( float2 sample )
{
    float z = 1 - 2 * sample.x;
    float r = SafeSqrt( 1 - z * z );
    float phi = 2 * PI * sample.y;
    return float3( r * cos( phi ), r * sin( phi ), z );
}

float UniformSpherePDF()
{
    return 1 / ( 4 * PI );
}

float PowerHeuristic( uint nf, float fPdf, uint ng, float gPdf )
{
    float f = nf * fPdf;
    float g = ng * gPdf;
    return ( f * f ) / ( f * f + g * g );
}

#endif