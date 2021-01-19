#ifndef _SAMPLES_H_
#define _SAMPLES_H_

uint GetSampleIndex( uint2 pixelPos )
{
    uint2 localSampleIndex = pixelPos % RT_SAMPLE_TILE_SIZE;
    return localSampleIndex.y * RT_SAMPLE_TILE_SIZE + localSampleIndex.x;
}

uint GetSampleIndexNextRayDepth( uint sampleIndex )
{
    return sampleIndex + RT_SAMPLE_TILE_SIZE * RT_SAMPLE_TILE_SIZE;
}

float2 GetPixelSample( uint sampleIndex )
{
    return g_PixelSamples[ sampleIndex ];
}

float GetLightSelectionSample( uint sampleIndex )
{
    return g_LightSelectionSamples[ sampleIndex ];
}

float GetBRDFSelectionSample( uint sampleIndex )
{
    return g_BRDFSelectionSamples[ sampleIndex ];
}

float2 GetBRDFSample( uint sampleIndex )
{
    return g_BRDFSamples[ sampleIndex ];
}

#endif