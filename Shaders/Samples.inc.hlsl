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

uint Interleave_32bit( uint2 pixelPos )
{
    uint x = pixelPos.x & 0x0000FFFF;           // x = ---- ---- ---- ---- fedc ba98 7654 3210
    uint y = pixelPos.y & 0x0000FFFF;

    x = ( x | ( x << 8 ) ) & 0x00FF00FF;        // x = ---- ---- fedc ba98 ---- ---- 7654 3210
    x = ( x | ( x << 4 ) ) & 0x0F0F0F0F;        // x = ---- fedc ---- ba98 ---- 7654 ---- 3210
    x = ( x | ( x << 2 ) ) & 0x33333333;        // x = --fe --dc --ba --98 --76 --54 --32 --10
    x = ( x | ( x << 1 ) ) & 0x55555555;        // x = -f-e -d-c -b-a -9-8 -7-6 -5-4 -3-2 -1-0

    y = ( y | ( y << 8 ) ) & 0x00FF00FF;
    y = ( y | ( y << 4 ) ) & 0x0F0F0F0F;
    y = ( y | ( y << 2 ) ) & 0x33333333;
    y = ( y | ( y << 1 ) ) & 0x55555555;

    return x | ( y << 1 );
}

#include "UInt64.inc.hlsl"

uint2 SplitMix64_NextRandom( inout uint2 rng )
{
    uint2 z = UInt64_Add( rng, uint2( 0x7F4A7C15, 0x9E3779B9 ) );
    rng = z;
    z = UInt64_Multiply( z ^ UInt64_ShiftRight( z, 30 ), uint2( 0x1CE4E5B9, 0xBF58476D ) );
    z = UInt64_Multiply( z ^ UInt64_ShiftRight( z, 27 ), uint2( 0x133111EB, 0x94D049BB ) );
    return z ^ UInt64_ShiftRight( z, 31 );
}

Xoshiro128StarStar InitializeRandomNumberGenerator( uint2 pixelPos, uint frameSeed )
{
    uint2 splitMix64 = uint2( Interleave_32bit( pixelPos ), frameSeed );
    uint2 s0 = SplitMix64_NextRandom( splitMix64 );
    uint2 s1 = SplitMix64_NextRandom( splitMix64 );
    Xoshiro128StarStar rng;
    rng.state[ 0 ] = s0.x;
    rng.state[ 1 ] = s0.y;
    rng.state[ 2 ] = s1.x;
    rng.state[ 3 ] = s1.y;
    return rng;
}

#endif