#ifndef _ENVIRONMENTSHADER_H_
#define _ENVIRONMENTSHADER_H_

float3 EnvironmentShader( float3 wi )
{
#if defined( NO_ENV_TEXTURE )
    return g_Background;
#else
    return g_EnvTexture.SampleLevel( UVClampSampler, wi, 0 ).rgb * g_Background;
#endif
}

#endif

