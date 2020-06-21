#ifndef _SAMPLES_H_
#define _SAMPLES_H_

StructuredBuffer<float> g_Samples : register( t3 );

groupshared uint gs_NextSampleIndex = 0;

float GetNextSample()
{
    uint sampleIndex;
    InterlockedAdd( gs_NextSampleIndex, 1, sampleIndex );
    sampleIndex = sampleIndex % g_SamplesCount;
    return g_Samples[ sampleIndex ];
}

float2 GetNextSample2()
{
    return float2( GetNextSample(), GetNextSample() );
}

#endif