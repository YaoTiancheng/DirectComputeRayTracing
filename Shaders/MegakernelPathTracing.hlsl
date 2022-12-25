#include "RayTracingCommon.inc.hlsl"

#if defined( MEGAKERNEL )

cbuffer RayTracingConstants : register( b0 )
{
    row_major float4x4 g_CameraTransform;
    float2 g_FilmSize;
    uint2 g_Resolution;
    uint g_MaxBounceCount;
    uint g_LightCount;
    float g_ApertureRadius;
    float g_FocalDistance;
    float g_FilmDistance;
    float2 g_BladeVertexPos;
    uint g_BladeCount;
    float g_ApertureBaseAngle;
    uint2 g_TileOffset;
    uint g_EnvironmentLightIndex;
}

StructuredBuffer<Vertex> g_Vertices                     : register( t0 );
StructuredBuffer<uint> g_Triangles                      : register( t1 );
StructuredBuffer<SLight> g_Lights                       : register( t2 );
Texture2D<float> g_CookTorranceCompETexture             : register( t3 );
Texture2D<float> g_CookTorranceCompEAvgTexture          : register( t4 );
Texture2D<float> g_CookTorranceCompInvCDFTexture        : register( t5 );
Texture2D<float> g_CookTorranceCompPdfScaleTexture      : register( t6 );
Texture2DArray<float> g_CookTorranceCompEFresnelTexture : register( t7 );
Texture2DArray<float> g_CookTorranceBSDFETexture        : register( t8 );
Texture2DArray<float> g_CookTorranceBSDFAvgETexture     : register( t9 );
Texture2DArray<float> g_CookTorranceBTDFETexture        : register( t10 );
Texture2DArray<float> g_CookTorranceBSDFInvCDFTexture   : register( t11 );
Texture2DArray<float> g_CookTorranceBSDFPDFScaleTexture : register( t12 );
StructuredBuffer<BVHNode> g_BVHNodes                    : register( t13 );
StructuredBuffer<float4x3> g_InstanceTransforms         : register( t14 );
StructuredBuffer<float4x3> g_InstanceInvTransforms      : register( t15 );
StructuredBuffer<uint> g_MaterialIds                    : register( t16 );
StructuredBuffer<Material> g_Materials                  : register( t17 );
Buffer<uint> g_InstanceLightIndices                     : register( t18 );
TextureCube<float3> g_EnvTexture                        : register( t19 );
RWTexture2D<float2> g_SamplePositionTexture             : register( u0 );
RWTexture2D<float3> g_SampleValueTexture                : register( u1 );

#include "BSDFs.inc.hlsl"

[ numthreads( GROUP_SIZE_X, GROUP_SIZE_Y, 1 ) ]
void main( uint threadId : SV_GroupIndex, uint2 pixelPos : SV_DispatchThreadID )
{
    pixelPos += g_TileOffset;
    if ( any( pixelPos > g_Resolution ) )
        return;

    Xoshiro128StarStar rng = InitializeRandomNumberGenerator( pixelPos, g_FrameSeed );

    float3 pathThroughput = 1.f;
    float3 l = 0.f;
    float3 wi, wo;
    Intersection intersection;

    float2 pixelSample = GetNextSample2D( rng );
    float2 filmSample = ( pixelSample + pixelPos ) / g_Resolution;
    float3 apertureSample = GetNextSample3D( rng );
    GenerateRay( filmSample, apertureSample, g_FilmSize, g_ApertureRadius, g_FocalDistance, g_FilmDistance, g_BladeCount, g_BladeVertexPos, g_ApertureBaseAngle, g_CameraTransform, intersection.position, wi );

    float hitDistance;
    uint iterationCounter;
    bool hasHit = IntersectScene( intersection.position, wi, threadId, g_Vertices, g_Triangles, g_BVHNodes, g_InstanceTransforms, g_InstanceInvTransforms, g_InstanceLightIndices, g_MaterialIds, g_Materials, intersection, hitDistance, iterationCounter );

    if ( hasHit )
    { 
#if defined( LIGHT_VISIBLE )
        if ( intersection.lightIndex != LIGHT_INDEX_INVALID )
        {
            l = TriangleLight_Evaluate( g_Lights[ intersection.lightIndex ], -wi, intersection.geometryNormal );
        }
#endif

        uint iBounce = 0;
        while ( iBounce <= g_MaxBounceCount && hasHit )
        {
            wo = -wi;

            // Sample light
            if ( g_LightCount != 0 )
            {
                SLightSampleResult sampleResult = SampleLightDirect( intersection.position, g_Lights, g_LightCount, g_Vertices, g_Triangles, g_InstanceTransforms, g_EnvTexture, UVClampSampler, rng );
                bool isDeltaLight = sampleResult.isDeltaLight;
                if ( any( sampleResult.radiance > 0.f ) && sampleResult.pdf > 0.f
                    && !IsOcculuded( OffsetRayOrigin( intersection.position, intersection.geometryNormal, sampleResult.wi ), sampleResult.wi, sampleResult.distance, threadId, g_Vertices, g_Triangles, g_BVHNodes, g_InstanceInvTransforms ) )
                {
                    float3 bsdf = EvaluateBSDF( sampleResult.wi, wo, intersection );
                    float NdotWI = abs( dot( intersection.normal, sampleResult.wi ) );
                    float bsdfPdf = EvaluateBSDFPdf( sampleResult.wi, wo, intersection );
                    float weight = isDeltaLight ? 1.0f : PowerHeuristic( 1, sampleResult.pdf, 1, bsdfPdf );
                    l += pathThroughput * sampleResult.radiance * bsdf * NdotWI * weight / sampleResult.pdf;
                }
            }

            // Sample BSDF
            {
                float bsdfSelectionSample = GetNextSample1D( rng );
                float2 bsdfSample = GetNextSample2D( rng );

                float3 bsdf;
                float bsdfPdf;
                bool isDeltaBxdf;
                SampleBSDF( wo, bsdfSample, bsdfSelectionSample, intersection, wi, bsdf, bsdfPdf, isDeltaBxdf );

                if ( all( bsdf == 0.0f ) || bsdfPdf == 0.0f )
                    break;

                float NdotWI = abs( dot( intersection.normal, wi ) );
                pathThroughput = pathThroughput * bsdf * NdotWI / bsdfPdf;

                hasHit = IntersectScene( OffsetRayOrigin( intersection.position, intersection.geometryNormal, wi ), wi, threadId, g_Vertices, g_Triangles, g_BVHNodes, g_InstanceTransforms, g_InstanceInvTransforms, g_InstanceLightIndices, g_MaterialIds, g_Materials, intersection, hitDistance, iterationCounter );

                uint lightIndex = hasHit ? intersection.lightIndex : g_EnvironmentLightIndex;
                if ( lightIndex != LIGHT_INDEX_INVALID )
                {
                    float3 radiance;
                    float lightPdf;
                    EvaluateLightDirect( lightIndex, intersection.triangleIndex, intersection.geometryNormal, wi, hitDistance, g_Lights, g_LightCount, g_Vertices, g_Triangles, g_InstanceTransforms, g_EnvTexture, UVClampSampler, radiance, lightPdf );
                    if ( lightPdf > 0.0f )
                    {
                        float weight = !isDeltaBxdf ? PowerHeuristic( 1, bsdfPdf, 1, lightPdf ) : 1.0f;
                        l += pathThroughput * radiance * weight;
                    }
                }
            }

            ++iBounce;
        }
    }
#if defined( LIGHT_VISIBLE )
    else if ( g_EnvironmentLightIndex != LIGHT_INDEX_INVALID )
    {
        l = EnvironmentLight_Evaluate( g_Lights[ g_EnvironmentLightIndex ], wi, g_EnvTexture, UVClampSampler );
    }
#endif

    WriteSample( l, pixelSample, pixelPos, g_SamplePositionTexture, g_SampleValueTexture );
}

#endif

#if defined( OUTPUT_NORMAL ) || defined( OUTPUT_TANGENT ) || defined( OUTPUT_ALBEDO ) || defined( OUTPUT_NEGATIVE_NDOTV ) || defined( OUTPUT_BACKFACE ) || defined( OUTPUT_ITERATION_COUNT )

cbuffer RayTracingConstants : register( b0 )
{
    row_major float4x4 g_CameraTransform;
    float2 g_FilmSize;
    uint2 g_Resolution;
    uint g_MaxBounceCount;
    uint g_LightCount;
    float g_ApertureRadius;
    float g_FocalDistance;
    float g_FilmDistance;
    float2 g_BladeVertexPos;
    uint g_BladeCount;
    float g_ApertureBaseAngle;
    uint2 g_TileOffset;
    uint g_EnvironmentLightIndex;
}

cbuffer DebugConstants : register( b2 )
{
    uint g_IterationThreshold;
}

StructuredBuffer<Vertex> g_Vertices     : register( t0 );
StructuredBuffer<uint> g_Triangles      : register( t1 );
StructuredBuffer<BVHNode> g_BVHNodes    : register( t13 );
StructuredBuffer<float4x3> g_InstanceTransforms : register( t14 );
StructuredBuffer<float4x3> g_InstanceInvTransforms : register( t15 );
StructuredBuffer<uint> g_MaterialIds    : register( t16 );
StructuredBuffer<Material> g_Materials  : register( t17 );
Buffer<uint> g_InstanceLightIndices     : register( t18 );
RWTexture2D<float2> g_SamplePositionTexture : register( u0 );
RWTexture2D<float3> g_SampleValueTexture    : register( u1 );

[ numthreads( GROUP_SIZE_X, GROUP_SIZE_Y, 1 ) ]
void main( uint threadId : SV_GroupIndex, uint2 pixelPos : SV_DispatchThreadID )
{
    pixelPos += g_TileOffset;
    if ( any( pixelPos > g_Resolution ) )
        return;

    Xoshiro128StarStar rng = InitializeRandomNumberGenerator( pixelPos, g_FrameSeed );

    float3 l = 0.0f;
    float3 wo;
    Intersection intersection;

    float2 pixelSample = GetNextSample2D( rng );
    float2 filmSample = ( pixelSample + pixelPos ) / g_Resolution;
    float3 apertureSample = GetNextSample3D( rng );
    GenerateRay( filmSample, apertureSample, g_FilmSize, g_ApertureRadius, g_FocalDistance, g_FilmDistance, g_BladeCount, g_BladeVertexPos, g_ApertureBaseAngle, g_CameraTransform, intersection.position, wo );

    float hitDistance = 0.0f;
    uint iterationCounter;
    if ( IntersectScene( intersection.position, wo, threadId, g_Vertices, g_Triangles, g_BVHNodes, g_InstanceTransforms, g_InstanceInvTransforms, g_InstanceLightIndices, g_MaterialIds, g_Materials, intersection, hitDistance, iterationCounter ) )
    {
#if defined( OUTPUT_NORMAL )
        l = intersection.normal * 0.5f + 0.5f;
#elif defined( OUTPUT_TANGENT )
        l = intersection.tangent * 0.5f + 0.5f;
#elif defined( OUTPUT_ALBEDO )
        l = intersection.albedo;
#elif defined( OUTPUT_NEGATIVE_NDOTV )
        l = dot( -wo, intersection.normal ) < 0.0f ? float3( 0.0f, 1.0f, 0.0f ) : float3( 1.0f, 0.0f, 0.0f );
#elif defined( OUTPUT_BACKFACE )
        l = intersection.backface ? float3( 0.0f, 1.0f, 0.0f ) : float3( 1.0f, 0.0f, 0.0f );
#endif
    }
#if defined( OUTPUT_ITERATION_COUNT )
    l = iterationCounter <= g_IterationThreshold ? iterationCounter / (float)g_IterationThreshold : float3( 1.f, 0.f, 0.f );
#endif

    WriteSample( l, pixelSample, pixelPos, g_SamplePositionTexture, g_SampleValueTexture );
}

#endif