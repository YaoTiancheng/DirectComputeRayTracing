#ifndef _LIGHT_H_
#define _LIGHT_H_

void PointLight_Sample( SLight light, float3 p, out float3 radiance, out float3 wi, out float distance, out float pdf )
{
    float3 lightPosWS = Light_GetPosition( light );
    wi = lightPosWS - p;
    distance = length( wi );
    wi /= distance;
    radiance = light.radiance / ( distance * distance );
    pdf = 1.0f;
}

void DirectionalLight_Sample( SLight light, out float3 radiance, out float3 wi, out float distance, out float pdf )
{
    wi = -Light_GetPosition( light );
    distance = FLT_INF;
    radiance = light.radiance;
    pdf = 1.0f;
}

float3 TriangleLight_Evaluate( SLight light, float3 direction, float3 normal )
{
    return dot( direction, normal ) > 0.f ? light.radiance : 0.f;
}

void TriangleLight_EvaluateWithPDF( SLight light, float4x3 transform, float3 v0, float3 v1, float3 v2, float3 wi, float3 normal, float distance, out float3 radiance, out float pdf )
{
    // Transform all vertices to world space before evaluation so we can calculate correct surface area use triangle edge vectors
    v0 = mul( float4( v0, 1.f ), transform );
    v1 = mul( float4( v1, 1.f ), transform );
    v2 = mul( float4( v2, 1.f ), transform );

    float3 v0v1 = v1 - v0;
    float3 v0v2 = v2 - v0;
    float3 crossProduct = cross( v0v2, v0v1 );
    float surfaceArea = length( crossProduct );
    pdf = surfaceArea >= 1e-6f ? 1.f / ( surfaceArea * .5f ) : 0.f;

    float WIdotN = -dot( wi, normal );
    radiance = WIdotN > 0.f ? light.radiance : 0.f;
    pdf *= WIdotN > 0.f ? distance * distance / dot( -wi, normal ) : 0.f;
}

void TriangleLight_Sample( SLight light, float4x3 transform, float3 v0, float3 v1, float3 v2, float2 samples, float3 p, out float3 radiance, out float3 wi, out float distance, out float pdf )
{
    // Transform all vertices to world space to calculate correct surface area use triangle edge vectors
    float3 vws0 = mul( float4( v0, 1.f ), transform );
    float3 vws1 = mul( float4( v1, 1.f ), transform );
    float3 vws2 = mul( float4( v2, 1.f ), transform );
    float surfaceArea = length( cross( vws2 - vws0, vws1 - vws0 ) ) * .5f;

    float2 samplePosBarycentric = SampleTriangle( samples );
    float3 samplePos = VectorBaryCentric3( v0, v1, v2, samplePosBarycentric.x, samplePosBarycentric.y );
    float3 v0v1 = v1 - v0;
    float3 v0v2 = v2 - v0;
    float3 crossProduct = cross( v0v2, v0v1 );
    float3 normal = normalize( crossProduct );
    pdf = surfaceArea >= 1e-6f ? 1.f / ( surfaceArea * .5f ) : 0.f;

    // Transform the sample position and normal to world space before we calculating wi
    samplePos = mul( float4( samplePos, 1.f ), transform );
    normal = normalize( mul( float4( normal, 0.f ), transform ) );

    wi = samplePos - p;
    distance = length( wi );
    wi /= distance;
    float WIdotN = -dot( wi, normal );
    pdf *= distance * distance / WIdotN;

    radiance = WIdotN > 0.f && pdf > 0.f ? light.radiance : 0.f;
    pdf = WIdotN > 0.f ? pdf : 0.f;
}

float3 EnvironmentLight_Evaluate( SLight light, float3 direction, TextureCube<float3> envTexture, SamplerState envTextureSampler )
{
#if defined( HAS_ENV_TEXTURE )
    return envTexture.SampleLevel( envTextureSampler, direction, 0 ).rgb * light.radiance;
#else 
    return light.radiance;
#endif
}

void EnvironmentLight_EvaluateWithPDF( SLight light, float3 wi, TextureCube<float3> envTexture, SamplerState envTextureSampler, out float3 radiance, out float pdf )
{
#if defined( HAS_ENV_TEXTURE )
    radiance = envTexture.SampleLevel( envTextureSampler, wi, 0 ).rgb * light.radiance;
#else 
    radiance = light.radiance;
#endif
    pdf = UniformSpherePDF();
}

void EnvironmentLight_Sample( SLight light, float2 samples, TextureCube<float3> envTexture, SamplerState envTextureSampler, out float3 radiance, out float3 wi, out float distance, out float pdf )
{
    wi = SampleSphere( samples );
    pdf = UniformSpherePDF();
#if defined( HAS_ENV_TEXTURE )
    radiance = envTexture.SampleLevel( envTextureSampler, wi, 0 ).rgb * light.radiance;
#else 
    radiance = light.radiance;
#endif
    distance = FLT_INF;
}



#endif