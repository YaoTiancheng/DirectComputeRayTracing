#ifndef _SAMPLES_H_
#define _SAMPLES_H_

StructuredBuffer<float> g_Samples : register( t3 );

float GetNextSample()
{
    uint sampleIndex;
    InterlockedAdd( g_SampleCounter[ 0 ], 1, sampleIndex );
    sampleIndex = sampleIndex % g_SamplesCount;
    return g_Samples[ sampleIndex ];
}

float2 GetNextSample2()
{
    uint sampleIndex;
    InterlockedAdd( g_SampleCounter[ 0 ], 2, sampleIndex );
    float2 sample;
    sample.x = g_Samples[ ( sampleIndex - 1 ) % g_SamplesCount ];
    sample.y = g_Samples[ sampleIndex % g_SamplesCount ];
    return sample;
}

#endif