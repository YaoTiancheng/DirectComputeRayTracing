#ifndef _INTRINSICS_H_
#define _INTRINSICS_H_

uint WavePrefixCountBits32( uint bitmask )
{
    return countbits( bitmask & ( ( 1U << WaveGetLaneIndex() ) - 1 ) );
}

#endif