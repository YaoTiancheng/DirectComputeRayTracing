#ifndef _ENVIRONMENTSHADER_H_
#define _ENVIRONMENTSHADER_H_

float3 EnvironmentShader( float3 wi, float3 background, TextureCube<float3> envTexture, SamplerState UVClampSampler )
{
#if defined( NO_ENV_TEXTURE )
    return background;
#else
    return envTexture.SampleLevel( UVClampSampler, wi, 0 ).rgb * background;
#endif
}

#endif

