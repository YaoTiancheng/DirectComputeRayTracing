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

#endif