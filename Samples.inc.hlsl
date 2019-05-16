
StructuredBuffer<float> g_Samples : register( t3 );

groupshared uint gs_NextSampleIndex = 0;

float GetNextSample()
{
    uint sampleIndex;
    InterlockedAdd( gs_NextSampleIndex, 1, sampleIndex );
    sampleIndex = sampleIndex % g_Constants[ 0 ].samplesCount;
    return g_Samples[ sampleIndex ];
}

float2 GetNextSample2()
{
    return float2( GetNextSample(), GetNextSample() );
}