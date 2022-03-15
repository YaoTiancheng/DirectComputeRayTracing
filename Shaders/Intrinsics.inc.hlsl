#ifndef _INTRINSICS_H_
#define _INTRINSICS_H_

#define WAVEFRONT_SIZE 32

uint WaveGetLaneIndex( uint groupIndex )
{
    return groupIndex % WAVEFRONT_SIZE;
}

bool WaveIsFirstLane( uint groupIndex )
{
    return WaveGetLaneIndex( groupIndex ) == 0;
}

uint WavePrefixCountBits32( uint laneIndex, uint bitmask )
{
    return countbits( shadowRayMask & ( 0xFFFFFFFF >> ( 32 - laneIndex ) ) );
}

// Following wave intrinsics emulation only works for group size equals to WAVEFRONT_SIZE, and it uses group shared memory

groupshared uint gs_Ballot;
uint WaveActiveBallot32( uint laneIndex, bool expr )
{
    uint original = 0;
    InterlockedOr( gs_Ballot, expr ? 1 << laneIndex : 0, original );
    GroupMemoryBarrierWithGroupSync();
    return gs_Ballot;
}

groupshared uint gs_FirstLaneResult;
uint WaveReadLaneFirst32( uint laneIndex, uint expr )
{
    if ( laneIndex == 0 )
    {
        gs_FirstLaneResult = expr;
    }
    GroupMemoryBarrierWithGroupSync();
    return gs_FirstLaneResult;
}

#endif