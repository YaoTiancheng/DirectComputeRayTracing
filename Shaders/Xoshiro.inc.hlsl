#ifndef _XOSHIRO_H_
#define _XOSHIRO_H_

struct Xoshiro128StarStar
{
    uint state[ 4 ];
};

uint Xoshiro_rotl( uint x, int k )
{
    return ( x << k ) | ( x >> ( 32 - k ) );
}

/** Generates the next pseudorandom number in the sequence (32 bits).
*/
uint Xoshiro_NextRandom( inout Xoshiro128StarStar rng )
{
    const uint result_starstar = Xoshiro_rotl( rng.state[ 0 ] * 5, 7 ) * 9;
    const uint t = rng.state[ 1 ] << 9;

    rng.state[ 2 ] ^= rng.state[ 0 ];
    rng.state[ 3 ] ^= rng.state[ 1 ];
    rng.state[ 1 ] ^= rng.state[ 2 ];
    rng.state[ 0 ] ^= rng.state[ 3 ];

    rng.state[ 2 ] ^= t;
    rng.state[ 3 ] = Xoshiro_rotl( rng.state[ 3 ], 11 );

    return result_starstar;
}

/** Jump function for the generator. It is equivalent to 2^64 calls to nextRandom().
    It can be used to generate 2^64 non-overlapping subsequences for parallel computations.
*/
void Xoshiro_Jump( inout Xoshiro128StarStar rng )
{
    const uint JUMP[] = { 0x8764000b, 0xf542d2d3, 0x6fa035c3, 0x77f2db5b };

    uint s0 = 0;
    uint s1 = 0;
    uint s2 = 0;
    uint s3 = 0;

    for ( int i = 0; i < 4; i++ )
    {
        for ( int b = 0; b < 32; b++ )
        {
            if ( JUMP[ i ] & ( 1u << b ) )
            {
                s0 ^= rng.state[ 0 ];
                s1 ^= rng.state[ 1 ];
                s2 ^= rng.state[ 2 ];
                s3 ^= rng.state[ 3 ];
            }
            Xoshiro_NextRandom( rng );
        }
    }

    rng.state[ 0 ] = s0;
    rng.state[ 1 ] = s1;
    rng.state[ 2 ] = s2;
    rng.state[ 3 ] = s3;
}

#endif