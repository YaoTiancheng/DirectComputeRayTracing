
#include "RayTracingDef.inc.hlsl"

cbuffer RayTracingConstants : register( b0 )
{
    uint                g_MaxBounceCount;
    uint                g_PrimitiveCount;
    uint                g_PointLightCount;
    uint                g_SamplesCount;
    uint2               g_Resolution;
    float2              g_FilmSize;
    float               g_FilmDistance;
    row_major float4x4  g_CameraTransform;
    float4              g_Background;
}

#include "Vertex.inc.hlsl"
#include "PointLight.inc.hlsl"
#include "Material.inc.hlsl"

StructuredBuffer<Vertex>                g_Vertices      : register( t0 );
StructuredBuffer<uint>                  g_Triangles     : register( t1 );
StructuredBuffer<PointLight>            g_PointLights   : register( t2 );
StructuredBuffer<uint>                  g_MaterialIds   : register( t10 );
StructuredBuffer<Material>              g_Materials     : register( t11 );
TextureCube<float3>                     g_EnvTexture    : register( t12 );
RWTexture2D<float4>                     g_FilmTexture   : register( u0 );
RWStructuredBuffer<uint>                g_SampleCounter : register( u1 );

SamplerState UVClampSampler;

#include "Samples.inc.hlsl"
#include "BSDFs.inc.hlsl"
#include "BVHAccel.inc.hlsl"
#include "HitShader.inc.hlsl"
#include "EnvironmentShader.inc.hlsl"

void GenerateRay( float2 sample
	, float2 filmSize
	, float filmDistance
	, float4x4 cameraTransform
	, out float3 origin
	, out float3 direction )
{
    float2 filmSample = float2( sample.x - 0.5f, -sample.y + 0.5f ) * filmSize;
    origin = float3( 0.0f, 0.0f, filmDistance );
    direction = normalize( origin - float3( filmSample, 0.0f ) );
    direction.xy = -direction.xy;

    origin = mul( float4( origin, 1.0f ), cameraTransform ).xyz;
    direction = mul( float4( direction, 0.0f ), cameraTransform ).xyz;
}

bool IntersectScene( float3 origin
	, float3 direction
    , float epsilon
    , uint dispatchThreadIndex
	, out Intersection intersection )
{
    return BVHIntersect( origin, direction, epsilon.x, dispatchThreadIndex, intersection );
}

bool IsOcculuded( float3 origin
    , float3 direction
    , float epsilon
    , float distance
    , uint dispatchThreadIndex )
{
    return BVHIntersect( origin, direction, epsilon.x, distance - epsilon.x, dispatchThreadIndex );
}

void AddSampleToFilm( float3 l
    , float2 sample
    , uint2 pixelPos )
{
    float4 c = g_FilmTexture[ pixelPos ];
    c += float4( l, 1.0f );
    g_FilmTexture[ pixelPos ] = c;
}

float3 EstimateDirect( PointLight pointLight, Intersection intersection, float epsilon, float3 wo, uint dispatchThreadIndex )
{
    float3 wi = pointLight.position - intersection.position;
    float len = length( wi );
    wi /= len;

    if ( !IsOcculuded( intersection.position, wi, epsilon, len, dispatchThreadIndex ) )
    {
        float3 l = pointLight.color / ( len * len );
        float3 brdf = EvaluateBSDF( wi, wo, intersection );
        float NdotL = max( 0, dot( intersection.normal, wi ) );
        return l * brdf * NdotL;
    }

    return 0.0f;
}

float3 UniformSampleOneLight( float sample, Intersection intersection, float3 wo, float epsilon, uint dispatchThreadIndex )
{
    if ( g_PointLightCount == 0 )
    {
        return 0.0f;
    }
    else
    {
        uint lightIndex = floor( GetNextSample() * g_PointLightCount );
        return EstimateDirect( g_PointLights[ lightIndex ], intersection, epsilon, wo, dispatchThreadIndex ) * g_PointLightCount;
    }
}

#define GROUP_SIZE_X 16
#define GROUP_SIZE_Y 16

[numthreads( GROUP_SIZE_X, GROUP_SIZE_Y, 1 )]
void main( uint threadId : SV_GroupIndex, uint2 pixelPos : SV_DispatchThreadID )
{
    if ( any( pixelPos > g_Resolution ) )
        return;

    float3 pathThroughput = 1.0f;
    float3 l = 0.0f;
    float3 wi, wo;
    Intersection intersection;

    float2 pixelSample = GetNextSample2();
    float2 filmSample = ( pixelSample + pixelPos ) / g_Resolution;
    GenerateRay( filmSample, g_FilmSize, g_FilmDistance, g_CameraTransform, intersection.position, wo );

    if ( IntersectScene( intersection.position, wo, 0.0f, threadId, intersection ) )
    {
        uint iBounce = 0;
        while ( 1 )
        {
            wo = -wo;

            float lightSelectionSample = GetNextSample();
            l += pathThroughput * ( UniformSampleOneLight( lightSelectionSample, intersection, wo, intersection.rayEpsilon, threadId ) + intersection.emission );

            if ( iBounce == g_MaxBounceCount )
                break;

            float3 brdf;
            float pdf;
            SampleBSDF( wo, GetNextSample2(), GetNextSample(), intersection, wi, brdf, pdf );

            if ( all( brdf == 0.0f ) || pdf == 0.0f )
                break;

            float NdotL = abs( dot( wi, intersection.normal ) );
            pathThroughput = pathThroughput * brdf * NdotL / pdf;

            if ( !IntersectScene( intersection.position, wi, intersection.rayEpsilon, threadId, intersection ) )
            {
                l += pathThroughput * EnvironmentShader( wi ) * g_Background;
                break;
            }
            else
            {
                wo = wi;
            }

            ++iBounce;
        }
    }
    else
    {
        l = EnvironmentShader( wo ) * g_Background;
    }

    AddSampleToFilm( l, pixelSample, pixelPos );
}