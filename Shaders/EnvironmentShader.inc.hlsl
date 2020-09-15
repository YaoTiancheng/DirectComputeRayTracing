#ifndef _ENVIRONMENTSHADER_H_
#define _ENVIRONMENTSHADER_H_

float3 EnvironmentShader( float3 wi )
{
    float red   = g_EnvTexture.GatherRed( UVClampSampler, wi ).x;
    float green = g_EnvTexture.GatherGreen( UVClampSampler, wi ).x;
    float blue  = g_EnvTexture.GatherBlue( UVClampSampler, wi ).x;
    return float3( red, green, blue );
}

#endif

