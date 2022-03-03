#ifndef _INTRINSICS_H_
#define _INTRINSICS_H_

#define WAVEFRONT_SIZE 32

uint WaveGetLaneIndex( uint groupIndex )
{
    return groupIndex % WAVEFRONT_SIZE;
}

uint WaveActiveBallot( uint groupIndex, bool expr )
{
    uint result = 0;
    uint laneIndex = WaveGetLaneIndex( groupIndex );
    uint original = 0;
    InterlockedOr( result, expr ? 1 << laneIndex : 0, original );
    return result;
}

bool WaveIsFirstLane( uint groupIndex )
{
    return WaveGetLaneIndex( groupIndex ) == 0;
}

uint WaveReadLaneFirst( uint expr )
{
    return 0;
}

#endif