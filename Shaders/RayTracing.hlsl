
struct PointLight
{
    float4              position;
    float4              color;
};

struct RayTracingConstants
{
    uint                maxBounceCount;
    uint                primitiveCount;
    uint                pointLightCount;
    uint                samplesCount;
    float2              resolution;
    float2              filmSize;
    float               filmDistance;
    row_major float4x4  cameraTransform;
    float4              background;
};

StructuredBuffer<RayTracingConstants>   g_Constants     : register( t3 );

#include "Samples.inc.hlsl"
#include "BSDFs.inc.hlsl"

StructuredBuffer<Vertex>                g_Vertices      : register( t0 );
StructuredBuffer<uint>                  g_Triangles     : register( t1 );
StructuredBuffer<PointLight>            g_PointLights   : register( t2 );
RWTexture2D<float4>                     g_FilmTexture;

#include "BVHAccel.inc.hlsl"

void GenerateRay( float2 sample
	, float2 filmSize
	, float filmDistance
	, float4x4 cameraTransform
	, out float3 origin
	, out float4 direction )
{
    float2 filmSample = float2( sample.x - 0.5f, -sample.y + 0.5f ) * filmSize;
    origin = float3( 0.0f, 0.0f, filmDistance );
    direction = float4( normalize( origin - float3( filmSample, 0.0f ) ), 0.0f );
    direction.xy = -direction.xy;

    origin = mul( float4( origin, 1.0f ), cameraTransform ).xyz;
    direction = mul( direction, cameraTransform );
}

bool IntersectScene( float3 origin
	, float3 direction
    , float4 epsilon
    , uint dispatchThreadIndex
	, out Intersection intersection )
{
    float t;
    return BVHIntersect( origin, direction, epsilon.x, dispatchThreadIndex, t, intersection );
}

bool IsOcculuded( float3 origin
    , float3 direction
    , float4 epsilon
    , float distance
    , uint dispatchThreadIndex )
{
    return BVHIntersect( origin, direction, epsilon.x, dispatchThreadIndex );
}

void AddSampleToFilm( float4 l
    , float2 sample
    , uint2 pixelPos )
{
    float4 c = g_FilmTexture[ pixelPos ];
    c += float4( l.xyz, 1.0f );
    g_FilmTexture[ pixelPos ] = c;
}

float4 EstimateDirect( PointLight pointLight, Intersection intersection, float epsilon, float4 wo, uint dispatchThreadIndex )
{
    float4 wi = pointLight.position - float4( intersection.position, 1.0f );
    float len = length( wi );
    wi /= len;

    if ( !IsOcculuded( intersection.position, wi.xyz, epsilon, len, dispatchThreadIndex ) )
    {
        float4 l = pointLight.color / ( len * len );
        float4 brdf = EvaluateBSDF( wi, wo, intersection );
        float NdotL = max( 0, dot( intersection.normal, wi ) );
        return l * brdf * NdotL;
    }

    return 0.0f;
}

float4 UniformSampleOneLight( float sample, Intersection intersection, float4 wo, float epsilon, uint dispatchThreadIndex )
{
    if ( g_Constants[ 0 ].pointLightCount == 0 )
    {
        return 0.0f;
    }
    else
    {
        uint lightIndex = floor( GetNextSample() * g_Constants[ 0 ].pointLightCount );
        return EstimateDirect( g_PointLights[ lightIndex ], intersection, epsilon, wo, dispatchThreadIndex ) * g_Constants[ 0 ].pointLightCount;
    }
}

#define GROUP_SIZE_X 16
#define GROUP_SIZE_Y 16

[numthreads( GROUP_SIZE_X, GROUP_SIZE_Y, 1 )]
void main( uint threadId : SV_GroupIndex, uint2 pixelPos : SV_DispatchThreadID )
{
    if ( any( pixelPos > g_Constants[ 0 ].resolution ) )
        return;

    float4 pathThroughput = ( float4 ) 1.0f;
    float4 l = ( float4 ) 0.0f;
    float4 wi, wo;
    Intersection intersection;

    float2 pixelSample = GetNextSample2();
    float2 filmSample = ( pixelSample + pixelPos ) / g_Constants[ 0 ].resolution;
    GenerateRay( filmSample, g_Constants[ 0 ].filmSize, g_Constants[ 0 ].filmDistance, g_Constants[ 0 ].cameraTransform, intersection.position, wo );

    if ( IntersectScene( intersection.position, wo.xyz, 0.0f, threadId, intersection ) )
    {
        uint iBounce = 0;
        while ( 1 )
        {
            wo = -wo;

            float lightSelectionSample = GetNextSample();
            l += pathThroughput * ( UniformSampleOneLight( lightSelectionSample, intersection, wo, intersection.rayEpsilon, threadId ) + intersection.emission );

            if ( iBounce == g_Constants[ 0 ].maxBounceCount )
                break;

            float4 brdf;
            float pdf;
            SampleBSDF( wo, GetNextSample2(), GetNextSample(), intersection, wi, brdf, pdf );

            if ( all( brdf.xyz == 0.0f ) || pdf == 0.0f )
                break;

            float NdotL = abs( dot( wi, intersection.normal ) );
            pathThroughput = pathThroughput * brdf * NdotL / pdf;

            if ( !IntersectScene( intersection.position, wi.xyz, intersection.rayEpsilon, threadId, intersection ) )
            {
                l += pathThroughput * g_Constants[ 0 ].background;
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
        l = g_Constants[ 0 ].background;
    }

    AddSampleToFilm( l, pixelSample, pixelPos );
}