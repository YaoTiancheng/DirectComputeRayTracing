#ifndef _INTRINSICS_H_
#define _INTRINSICS_H_

uint WavePrefixCountBits32( uint bitmask )
{
    return countbits( bitmask & ( ( 1U << WaveGetLaneIndex() ) - 1 ) );
}

uint MaxComponentIndexFloat3( float3 vector )
{
    int index = vector.x >= vector.y ? 0 : 1;
    index = vector[ index ] >= vector.z ? index : 2;
    return index;
}

float3 PermuteFloat3( float3 original, uint3 permute )
{
    float3 result;
    result.x = original[ permute.x ];
    result.y = original[ permute.y ];
    result.z = original[ permute.z ];
    return result;
}

#endif