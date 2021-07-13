#ifndef _ENVIRONMENTSHADER_H_
#define _ENVIRONMENTSHADER_H_

float3 EnvironmentShader( float3 wi )
{
    return g_EnvTexture.SampleLevel( UVClampSampler, wi, 0 ).rgb;
}

#endif

