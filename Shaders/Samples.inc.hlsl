#ifndef _SAMPLES_H_
#define _SAMPLES_H_

float GetNextSample1D( inout Xoshiro128StarStar rng )
{
    // Use upper 24 bits and divide by 2^24 to get a number u in [0,1).
    // In floating-point precision this also ensures that 1.0-u != 0.0.
    uint bits = Xoshiro_NextRandom( rng );
    return ( bits >> 8 ) / float( 1 << 24 );
}

float2 GetNextSample2D( inout Xoshiro128StarStar rng )
{
    float2 sample;
    // Don't use the float2 initializer to ensure consistent order of evaluation.
    sample.x = GetNextSample1D( rng );
    sample.y = GetNextSample1D( rng );
    return sample;
}

#endif