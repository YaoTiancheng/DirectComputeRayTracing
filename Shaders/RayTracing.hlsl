
#define FLT_INF asfloat( 0x7f800000 )

#include "Vertex.inc.hlsl"
#include "Light.inc.hlsl"
#include "Material.inc.hlsl"
#include "BVHNode.inc.hlsl"
#include "Xoshiro.inc.hlsl"

#include "Samples.inc.hlsl"
#include "BVHAccel.inc.hlsl"
#include "HitShader.inc.hlsl"
#include "EnvironmentShader.inc.hlsl"
#include "LightSampling.inc.hlsl"
#include "MonteCarlo.inc.hlsl"
#include "Intrinsics.inc.hlsl"

SamplerState UVClampSampler;

cbuffer RayTracingFrameConstants : register( b1 )
{
    uint g_FrameSeed;
}

// Based on "A Fast and Robust Method for Avoiding Self Intersection" by Carsten Wächter and Nikolaus Binder
float3 OffsetRayOrigin( float3 p, float3 n, float3 d )
{
    static float s_Origin = 1.0f / 32.0f;
    static float s_FloatScale = 1.0f / 65536.0f;
    static float s_IntScale = 256.0f;

    // Ray could be either leaving or entering the surface.
    // So if the ray is entering the surface make the origin offset along the negative normal direction.
    n *= sign( dot( n, d ) );

    int3 of_i = int3( s_IntScale * n );
    float3 p_i = asfloat( asint( p ) + ( ( p < 0 ) ? -of_i : of_i ) );
    return abs( p ) < s_Origin ? p + s_FloatScale * n : p_i;
}

float2 SampleAperture( float3 samples, float apertureRadius, uint bladeCount, float2 vertexPos, float bladeAngle, float baseAngle )
{
    if ( bladeCount <= 2 )
    {
        return ConcentricSampleDisk( samples.xy ) * apertureRadius;
    }
    else
    {
        // First sample the identity triangle, get the point p and then rotate it to the sampling blade.
        float2 uv = SampleTriangle( samples.xy );
        float2 p = float2( vertexPos.x * ( uv.x + uv.y ), vertexPos.y * uv.x - vertexPos.y * uv.y );
        float n = floor( samples.z * bladeCount );
        float theta = n * bladeAngle + baseAngle;
        float2 pRotated = float2( p.x * cos( theta ) - p.y * sin( theta )
            , p.y * cos( theta ) + p.x * sin( theta ) );
        return pRotated;
    }
}

void GenerateRay( float2 filmSample
    , float3 apertureSample
    , float2 filmSize
    , float apertureRadius
    , float focalDistance
    , float filmDistance
    , uint bladeCount
    , float2 bladeVertexPos
    , float apertureBaseAngle
    , float4x4 cameraTransform
    , out float3 origin
    , out float3 direction )
{
    float3 filmPos = float3( -filmSample.x + 0.5f, filmSample.y - 0.5f, -filmDistance );
    filmPos.xy *= filmSize;

    origin = float3( 0.0f, 0.0f, 0.0f );
    direction = normalize( -filmPos );

    if ( apertureRadius > 0.0f )
    {
        float3 aperturePos = float3( SampleAperture( apertureSample, apertureRadius, bladeCount, bladeVertexPos, PI_MUL_2 / bladeCount, apertureBaseAngle ), 0.0f );
        float3 focusPoint = direction * ( focalDistance / direction.z );
        origin = aperturePos;
        direction = normalize( focusPoint - origin );
    }

    origin = mul( float4( origin, 1.0f ), cameraTransform ).xyz;
    direction = mul( float4( direction, 0.0f ), cameraTransform ).xyz;
}

void HitInfoToIntersection( float3 origin
    , float3 direction
    , SHitInfo hitInfo
    , StructuredBuffer<Vertex> vertices
    , StructuredBuffer<uint> triangles
    , StructuredBuffer<uint> materialIds
    , StructuredBuffer<Material> materials
    , out Intersection intersection )
{
    Vertex v0 = vertices[ triangles[ hitInfo.triangleId * 3 ] ];
    Vertex v1 = vertices[ triangles[ hitInfo.triangleId * 3 + 1 ] ];
    Vertex v2 = vertices[ triangles[ hitInfo.triangleId * 3 + 2 ] ];
    HitShader( origin, direction, v0, v1, v2, hitInfo.t, hitInfo.u, hitInfo.v, hitInfo.triangleId, hitInfo.backface, materialIds, materials, intersection );
}

bool IntersectScene( float3 origin
    , float3 direction
    , uint dispatchThreadIndex
    , StructuredBuffer<Vertex> vertices
    , StructuredBuffer<uint> triangles
    , StructuredBuffer<BVHNode> BVHNodes
    , StructuredBuffer<uint> materialIds
    , StructuredBuffer<Material> materials
    , uint primitiveCount
    , inout Intersection intersection
    , out float t )
{
    SHitInfo hitInfo = (SHitInfo)0;
    t = FLT_INF;
    bool hasIntersection = BVHIntersectNoInterp( origin, direction, 0, dispatchThreadIndex, vertices, triangles, BVHNodes, primitiveCount, hitInfo );
    if ( hasIntersection )
    {
        t = hitInfo.t;
        HitInfoToIntersection( origin, direction, hitInfo, vertices, triangles, materialIds, materials, intersection );
    }
    return hasIntersection;
}

bool IsOcculuded( float3 origin
    , float3 direction
    , float distance
    , uint dispatchThreadIndex
    , StructuredBuffer<Vertex> vertices
    , StructuredBuffer<uint> triangles
    , StructuredBuffer<BVHNode> BVHNodes
    , uint primitiveCount )
{
    return BVHIntersect( origin, direction, 0, distance, dispatchThreadIndex, vertices, triangles, BVHNodes, primitiveCount );
}

void AddSampleToFilm( float3 l
    , float2 sample
    , uint2 pixelPos
    , RWTexture2D<float4> filmTexture )
{
    float4 c = filmTexture[ pixelPos ];
    c += float4( l, 1.0f );
    filmTexture[ pixelPos ] = c;
}

struct SLightSampleResult
{
    float3 radiance;
    float3 wi;
    float pdf;
    float distance;
    bool isDeltaLight;
};

SLightSampleResult SampleLight( SLight light, Intersection intersection, float2 samples, float3 wo )
{
    SLightSampleResult result;

    result.isDeltaLight = false;
    if ( light.flags & LIGHT_FLAGS_POINT_LIGHT )
    {
        SamplePointLight( light, samples, intersection.position, result.radiance, result.wi, result.distance, result.pdf );
        result.isDeltaLight = true;
    }
    else
    {
        SampleRectangleLight( light, samples, intersection.position, result.radiance, result.wi, result.distance, result.pdf );
    }

    return result;
}

void EvaluateLight( SLight light, float3 origin, float3 direction, out float distance, out float3 radiance, out float pdf )
{
    radiance = light.color;
    pdf = EvaluateRectangleLightPdf( light, direction, origin, distance );
}

struct SRay
{
    float3 origin;
    float3 direction;
    float  tMax;
    float  tMin;
};

struct SRayHit
{
    float  t;
    float2 uv;
    uint   triangleId;
};

struct SMISResults
{
    float3 lightResult;
    float3 bsdfResult;
    float lightDistance;
};

struct SPathAccumulation
{
    float3 pathThroughput;
    float3 Li;
};

#if defined( EXTENSION_RAY_CAST )

cbuffer QueueConstants : register( b0 )
{
    uint g_RayCount;
    uint g_ShadowRayCount;
}

cbuffer BVHConstants : register( b1 )
{
    uint g_PrimitiveCount;
}

StructuredBuffer<Vertex>      g_Vertices      : register( t0 );
StructuredBuffer<uint>        g_Triangles     : register( t1 );
StructuredBuffer<BVHNode>     g_BVHNodes      : register( t2 );
StructuredBuffer<SRay>        g_Rays          : register( t3 );
Buffer<uint>                  g_PathIndices   : register( t4 );
RWStructuredBuffer<SRayHit>   g_RayHits       : register( u0 );

[numthreads( 32, 1, 1 )]
void main( uint threadId : SV_DispatchThreadID, uint gtid : SV_GroupThreadID )
{
    if ( threadId >= g_RayCount )
        return;

    uint pathIndex = g_PathIndices[ threadId ];
    SRay ray = g_Rays[ pathIndex ];
    SHitInfo hitInfo;
    bool hasHit = BVHIntersectNoInterp( ray.origin
        , ray.direction
        , 0
        , gtid
        , g_Vertices
        , g_Triangles
        , g_BVHNodes
        , g_PrimitiveCount
        , hitInfo );
    SRayHit rayHit;
    rayHit.t = hasHit ? hitInfo.t : FLT_INF;
    rayHit.uv = float2( hitInfo.u, hitInfo.v );
    rayHit.triangleId = ( hitInfo.triangleId & 0x7FFFFFFF ) | ( hitInfo.backface ? 1 << 31 : 0 );

    g_RayHits[ pathIndex ] = rayHit;
}

#endif

#if defined( SHADOW_RAY_CAST )

cbuffer QueueConstants : register( b0 )
{
    uint g_RayCount;
    uint g_ShadowRayCount;
}

cbuffer BVHConstants : register( b1 )
{
    uint g_PrimitiveCount;
}

StructuredBuffer<Vertex>      g_Vertices      : register( t0 );
StructuredBuffer<uint>        g_Triangles     : register( t1 );
StructuredBuffer<BVHNode>     g_BVHNodes      : register( t2 );
StructuredBuffer<SRay>        g_Rays          : register( t3 );
Buffer<uint>                  g_PathIndices   : register( t4 );
RWStructuredBuffer<bool>      g_HasHits       : register( u0 );

[numthreads( 32, 1, 1 )]
void main( uint threadId : SV_DispatchThreadID, uint gtid : SV_GroupThreadID )
{
    if ( threadId >= g_ShadowRayCount )
        return;

    uint pathIndex = g_PathIndices[ threadId ];
    SRay ray = g_Rays[ pathIndex ];
    bool hasHit = BVHIntersect( ray.origin
        , ray.direction
        , 0.f
        , ray.tMax
        , gtid
        , g_Vertices
        , g_Triangles
        , g_BVHNodes
        , g_PrimitiveCount );

    g_HasHits[ pathIndex ] = hasHit;
}

#endif

#if defined( NEW_PATH )

cbuffer QueueConstants : register( b0 )
{
    uint               g_MaterialRayCount;
    uint               g_NewPathRayCount;
}

cbuffer CameraConstants : register( b2 )
{
    row_major float4x4 g_CameraTransform;
    uint2 g_Resolution;
    float2 g_FilmSize;
    float g_ApertureRadius;
    float g_FocalDistance;
    float g_FilmDistance;
    uint g_BladeCount;
    float2 g_BladeVertexPos;
    float g_ApertureBaseAngle;
}

Buffer<uint> g_PathIndices                      : register( t0 );
StructuredBuffer<uint2> g_PixelPositions        : register( t1 );
RWStructuredBuffer<SRay> g_Rays                 : register( u0 );
RWStructuredBuffer<float2> g_PixelSamples       : register( u1 );
RWStructuredBuffer<SMISResults> g_MISResults    : register( u2 );
RWStructuredBuffer<Xoshiro128StarStar> g_Rngs   : register( u3 );
RWBuffer<uint> g_ExtensionRayQueue              : register( u4 );
RWBuffer<uint> g_RayCounter                     : register( u5 );

[numthreads( 32, 1, 1 )]
void main( uint threadId : SV_DispatchThreadID, uint3 groupId : SV_GroupID, uint gtid : SV_GroupThreadID )
{
    if ( threadId >= g_NewPathRayCount )
        return;

    uint pathIndex = g_PathIndices[ threadId ];
    uint2 pixelPos = g_PixelPositions[ pathIndex ];

    Xoshiro128StarStar rng = InitializeRandomNumberGenerator( pixelPos, g_FrameSeed );

    float2 pixelSample = GetNextSample2D( rng );
    float2 filmSample = ( pixelSample + pixelPos ) / g_Resolution;
    float3 apertureSample = GetNextSample3D( rng );
    float3 origin = 0.0f;
    float3 direction = 0.0f;
    GenerateRay( filmSample, apertureSample, g_FilmSize, g_ApertureRadius, g_FocalDistance, g_FilmDistance, g_BladeCount, g_BladeVertexPos, g_ApertureBaseAngle, g_CameraTransform, origin, direction );

    g_PixelSamples[ pathIndex ] = pixelSample;
    g_MISResults[ pathIndex ] = (SMISResults)0;
    g_Rngs[ pathIndex ] = rng;

    SRay ray;
    ray.origin = origin;
    ray.direction = direction;
    ray.tMin = 0.0f;
    ray.tMax = FLT_INF;
    g_Rays[ pathIndex ] = ray;

    uint groupCount = g_NewPathRayCount / 32;
    if ( g_NewPathRayCount % 32 != 0 ) 
        groupCount++;
    uint lastGroupIndex = groupCount - 1;
    uint activeLaneCount = groupId.x != lastGroupIndex ? 32 : ( g_NewPathRayCount - lastGroupIndex * 32 );
    uint rayIndexBase = 0;
    if ( WaveIsFirstLane( gtid ) )
    {
        InterlockedAdd( g_RayCounter[ 0 ], activeLaneCount, rayIndexBase );
    }

    uint laneIndex = WaveGetLaneIndex( gtid );
    rayIndexBase = WaveReadLaneFirst32( laneIndex, rayIndexBase );
    g_ExtensionRayQueue[ rayIndexBase + laneIndex ] = pathIndex;
}

#endif

#if defined( MATERIAL )

cbuffer QueueConstants : register( b0 )
{
    uint               g_MaterialRayCount;
    uint               g_NewPathRayCount;
}

cbuffer MaterialConstants : register( b1 )
{
    uint               g_LightCount;
}

Buffer<uint>                                g_PathIndices                       : register( t0 );
StructuredBuffer<SRayHit>                   g_RayHits                           : register( t1 );
StructuredBuffer<Vertex>                    g_Vertices                          : register( t2 );
StructuredBuffer<uint>                      g_Triangles                         : register( t3 );
StructuredBuffer<SLight>                    g_Lights                            : register( t4 );
StructuredBuffer<uint>                      g_MaterialIds                       : register( t5 );
StructuredBuffer<Material>                  g_Materials                         : register( t6 );
Texture2D<float>                            g_CookTorranceCompETexture          : register( t7 );
Texture2D<float>                            g_CookTorranceCompEAvgTexture       : register( t8 );
Texture2D<float>                            g_CookTorranceCompInvCDFTexture     : register( t9 );
Texture2D<float>                            g_CookTorranceCompPdfScaleTexture   : register( t10 );
Texture2DArray<float>                       g_CookTorranceCompEFresnelTexture   : register( t11 );
Texture2DArray<float>                       g_CookTorranceBSDFETexture          : register( t12 );
Texture2DArray<float>                       g_CookTorranceBSDFAvgETexture       : register( t13 );
Texture2DArray<float>                       g_CookTorranceBTDFETexture          : register( t14 );
Texture2DArray<float>                       g_CookTorranceBSDFInvCDFTexture     : register( t15 );
Texture2DArray<float>                       g_CookTorranceBSDFPDFScaleTexture   : register( t16 );

RWStructuredBuffer<SRay>                    g_Rays                              : register( u0 );
RWStructuredBuffer<SRay>                    g_ShadowRays                        : register( u1 );
RWStructuredBuffer<Xoshiro128StarStar>      g_Rngs                              : register( u2 );
RWStructuredBuffer<SMISResults>             g_MISResults                        : register( u3 );
RWStructuredBuffer<SPathAccumulation>       g_PathAccumulation                  : register( u4 );
RWBuffer<uint>                              g_RayCounters                       : register( u5 );
RWBuffer<uint>                              g_ExtensionRayQueue                 : register( u6 );
RWBuffer<uint>                              g_ShadowRayQueue                    : register( u7 );

#include "BSDFs.inc.hlsl"

[numthreads( 32, 1, 1 )]
void main( uint threadId : SV_DispatchThreadID, uint gtid : SV_GroupThreadID )
{
    if ( threadId >= g_MaterialRayCount )
        return;

    uint pathIndex = g_PathIndices[ threadId ];

    SRayHit rayHit = g_RayHits[ pathIndex ];
    SHitInfo hitInfo;
    hitInfo.t = rayHit.t;
    hitInfo.u = rayHit.uv.x;
    hitInfo.v = rayHit.uv.y;
    hitInfo.triangleId = rayHit.triangleId & 0x7FFFFFFF;
    hitInfo.backface = ( rayHit.triangleId >> 31 ) != 0;

    SRay ray = g_Rays[ pathIndex ];
    float3 origin = ray.origin;
    float3 direction = ray.direction;
    Intersection intersection;
    HitInfoToIntersection( origin, direction, hitInfo, g_Vertices, g_Triangles, g_MaterialIds, g_Materials, intersection );

    Xoshiro128StarStar rng = g_Rngs[ pathIndex ];

    SPathAccumulation pathAccumulation = g_PathAccumulation[ pathIndex ];
    float3 pathThroughput = pathAccumulation.pathThroughput;

    pathAccumulation.Li += pathThroughput * intersection.emission;

    float3 wo = -direction;

    // Sample light
    bool isDeltaLight = false;
    uint lightIndex = 0;
    float3 lightResult = 0.0f;
    bool hasShadowRay = false;
    if ( g_LightCount != 0 )
    {
        float lightSelectionSample = GetNextSample1D( rng );
        float2 lightSamples = GetNextSample2D( rng );

        lightIndex = floor( lightSelectionSample * g_LightCount );
        SLightSampleResult sampleResult = SampleLight( g_Lights[ lightIndex ], intersection, lightSamples, wo );
        isDeltaLight = sampleResult.isDeltaLight;

        if ( any( sampleResult.radiance > 0.0f ) && sampleResult.pdf > 0.0f )
        {
            float3 bsdf = EvaluateBSDF( sampleResult.wi, wo, intersection );
            float NdotWI = abs( dot( intersection.normal, sampleResult.wi ) );
            float bsdfPdf = EvaluateBSDFPdf( sampleResult.wi, wo, intersection );
            float weight = isDeltaLight ? 1.0f : PowerHeuristic( 1, sampleResult.pdf, 1, bsdfPdf );
            lightResult = pathThroughput * sampleResult.radiance * bsdf * NdotWI * weight * g_LightCount / sampleResult.pdf;

            SRay shadowRay;
            shadowRay.direction = sampleResult.wi;
            shadowRay.origin = OffsetRayOrigin( intersection.position, intersection.geometryNormal, sampleResult.wi );
            shadowRay.tMin = 0.0f;
            shadowRay.tMax = sampleResult.distance;
            g_ShadowRays[ pathIndex ] = shadowRay;
            
            hasShadowRay = true;
        }
    }

    // Sample BSDF
    float3 bsdfResult = 0.0f;
    float3 wi;
    float lightDistance = FLT_INF;
    bool hasExtensionRay = false;
    {
        float bsdfSelectionSample = GetNextSample1D( rng );
        float2 bsdfSample = GetNextSample2D( rng );

        float3 bsdf;
        float bsdfPdf;
        bool isDeltaBxdf;
        SampleBSDF( wo, bsdfSample, bsdfSelectionSample, intersection, wi, bsdf, bsdfPdf, isDeltaBxdf );

        if ( any( bsdf != 0.0f ) && bsdfPdf != 0.0f )
        {
            float NdotWI = abs( dot( intersection.normal, wi ) );
            pathThroughput = pathThroughput * bsdf * NdotWI / bsdfPdf;

            ray.direction = wi;
            ray.origin = OffsetRayOrigin( intersection.position, intersection.geometryNormal, wi );
            ray.tMin = 0.0f;
            ray.tMax = FLT_INF;
            g_Rays[ pathIndex ] = ray;
            hasExtensionRay = true;

            if ( g_LightCount != 0 && !isDeltaLight )
            {
                float3 radiance;
                float lightPdf;
                EvaluateLight( g_Lights[ lightIndex ], intersection.position, wi, lightDistance, radiance, lightPdf );
                if ( lightPdf > 0.0f )
                {
                    float weight = !isDeltaBxdf ? PowerHeuristic( 1, bsdfPdf, 1, lightPdf ) : 1.0f;
                    bsdfResult = pathThroughput * radiance * weight * g_LightCount;
                }
            }
        }
        else
        {
            pathThroughput = 0.0f;
        }
    }

    SMISResults misResults;
    misResults.lightResult = lightResult;
    misResults.bsdfResult = bsdfResult;
    misResults.lightDistance = lightDistance;
    g_MISResults[ pathIndex ] = misResults;
    g_Rngs[ pathIndex ] = rng;
    pathAccumulation.pathThroughput = pathThroughput;
    g_PathAccumulation[ pathIndex ] = pathAccumulation;

    uint laneIndex = WaveGetLaneIndex( gtid );
    // Write extension ray indices
    {
        uint rayMask = WaveActiveBallot32( laneIndex, hasExtensionRay );
        uint rayCount = countbits( rayMask );
        uint rayIndexBase = 0;
        if ( laneIndex == 0 )
        {
            InterlockedAdd( g_RayCounters[ 0 ], rayCount, rayIndexBase );
        }

        rayIndexBase = WaveReadLaneFirst32( laneIndex, rayIndexBase );
        uint rayIndexOffset = WavePrefixCountBits32( laneIndex, rayMask );
        if ( hasExtensionRay )
        {
            g_ExtensionRayQueue[ rayIndexBase + rayIndexOffset ] = pathIndex;
        }
    }
    // Write shadow ray indices
    {
        uint rayMask = WaveActiveBallot32( laneIndex, hasShadowRay );
        uint rayCount = countbits( rayMask );
        uint rayIndexBase = 0;
        if ( laneIndex == 0 )
        {
            InterlockedAdd( g_RayCounters[ 1 ], rayCount, rayIndexBase );
        }

        rayIndexBase = WaveReadLaneFirst32( laneIndex, rayIndexBase );
        uint rayIndexOffset = WavePrefixCountBits32( laneIndex, rayMask );
        if ( hasShadowRay )
        {
            g_ShadowRayQueue[ rayIndexBase + rayIndexOffset ] = pathIndex;
        }
    }
}

#endif

#if defined( CONTROL )

cbuffer ControlConstants : register( b0 )
{
    float4             g_Background;
    uint               g_PathCount;
    uint               g_MaxBounceCount;
    uint2              g_BlockCounts;
    uint2              g_BlockDimension;
    uint2              g_FilmDimension;
}

StructuredBuffer<SRayHit>       g_RayHits           : register( t0 );
StructuredBuffer<SRay>          g_Rays              : register( t1 );
StructuredBuffer<float2>        g_PixelSamples      : register( t2 );
StructuredBuffer<bool>          g_HasShadowRayHits  : register( t3 );
StructuredBuffer<SMISResults>   g_MISResults        : register( t4 );
TextureCube<float3>             g_EnvTexture        : register( t5 );

RWStructuredBuffer<uint2>       g_PixelPositions    : register( u0 );
RWStructuredBuffer<SPathAccumulation> g_PathAccumulation : register( u1 );
RWStructuredBuffer<uint>        g_Bounces           : register( u2 );
RWBuffer<uint>                  g_QueueCounters     : register( u3 );
RWBuffer<uint>                  g_MaterialQueue     : register( u4 );
RWBuffer<uint>                  g_NewPathQueue      : register( u5 );
RWBuffer<uint>                  g_NextBlockIndex    : register( u6 );
RWTexture2D<float4>             g_FilmTexture       : register( u7 );

[numthreads( 32, 1, 1 )]
void main( uint threadId : SV_DispatchThreadID, uint gtid : SV_GroupThreadID )
{
    uint iBounce = g_Bounces[ threadId ];
    bool isIdle = ( iBounce & 0x80000000 ) != 0 || threadId >= g_PathCount;
    if ( !isIdle )
    {
        iBounce = iBounce & 0x7FFFFFFF;

        float3 wi = g_Rays[ threadId ].direction;
        SPathAccumulation pathAccumulation = g_PathAccumulation[ threadId ];
        SRayHit rayHit = g_RayHits[ threadId ];
        bool hasShadowRayHit = g_HasShadowRayHits[ threadId ];
        SMISResults misResults = g_MISResults[ threadId ];

        float3 lightResult = misResults.lightResult;
        pathAccumulation.Li += !hasShadowRayHit ? lightResult : 0.0f;

        float3 bsdfResult = misResults.bsdfResult;
        float lightDistance = misResults.lightDistance;
        pathAccumulation.Li += lightDistance < rayHit.t ? bsdfResult : 0.0f;

        if ( iBounce > g_MaxBounceCount || all( pathAccumulation.pathThroughput == 0.0f ) )
        {
            isIdle = true;
        }
        else if ( rayHit.t == FLT_INF )
        {
            pathAccumulation.Li += pathAccumulation.pathThroughput * EnvironmentShader( wi, g_Background.xyz, g_EnvTexture, UVClampSampler );
            isIdle = true;
        }
        else
        {
            g_PathAccumulation[ threadId ] = pathAccumulation;
            ++iBounce;
        }

        if ( isIdle )
        {
            uint2 pixelPosition = g_PixelPositions[ threadId ];
            float2 pixelSample = g_PixelSamples[ threadId ];
            AddSampleToFilm( pathAccumulation.Li, pixelSample, pixelPosition, g_FilmTexture );
        }
    }

    uint laneIndex = WaveGetLaneIndex( gtid );
    uint nonIdleLaneMask = WaveActiveBallot32( laneIndex, !isIdle );
    uint nonIdleLaneCount = countbits( nonIdleLaneMask );
    // Write material queue
    if ( nonIdleLaneCount > 0 )
    {
        uint rayIndexBase = 0;
        if ( laneIndex == 0 )
        {
            InterlockedAdd( g_QueueCounters[ 0 ], nonIdleLaneCount, rayIndexBase );
        }

        rayIndexBase = WaveReadLaneFirst32( laneIndex, rayIndexBase );
        uint rayIndexOffset = WavePrefixCountBits32( laneIndex, nonIdleLaneMask );
        if ( !isIdle )
        {
            g_MaterialQueue[ rayIndexBase + rayIndexOffset ] = threadId;
        }
    }
    else // Generate new paths when the entire wavefront is idle
    {
        uint nextBlockIndex = 0;
        if ( laneIndex == 0 )
        {
            InterlockedAdd( g_NextBlockIndex[ 0 ], 1, nextBlockIndex );
        }

        uint totalBlockCount = g_BlockCounts.x * g_BlockCounts.y;
        nextBlockIndex = WaveReadLaneFirst32( laneIndex, nextBlockIndex );
        if ( nextBlockIndex < totalBlockCount )
        {
            uint blockPosX = nextBlockIndex % g_BlockCounts.x;
            uint blockPosY = nextBlockIndex / g_BlockCounts.x;
            uint lanePosX = laneIndex % g_BlockDimension.x;
            uint lanePosY = laneIndex / g_BlockDimension.x;
            uint pixelPosX = blockPosX * g_BlockDimension.x + lanePosX;
            uint pixelPosY = blockPosY * g_BlockDimension.y + lanePosY;
            bool isClipped = pixelPosX >= g_FilmDimension.x || pixelPosY >= g_FilmDimension.y;

            uint nonClipedLaneMask = WaveActiveBallot32( laneIndex, !isClipped );
            uint nonClipedLaneCount = countbits( nonClipedLaneMask );
            uint rayIndexBase = 0;
            if ( laneIndex == 0 )
            {
                InterlockedAdd( g_QueueCounters[ 1 ], nonClipedLaneCount, rayIndexBase );
            }

            rayIndexBase = WaveReadLaneFirst32( laneIndex, rayIndexBase );
            uint rayIndexOffset = WavePrefixCountBits32( laneIndex, nonClipedLaneMask );
            if ( !isClipped )
            {
                g_PixelPositions[ threadId ] = uint2( pixelPosX, pixelPosY );
                SPathAccumulation pathAccumulation;
                pathAccumulation.Li = 0.0f;
                pathAccumulation.pathThroughput = 1.0f;
                g_PathAccumulation[ threadId ] = pathAccumulation;
                g_NewPathQueue[ rayIndexBase + rayIndexOffset ] = threadId;
                isIdle = false;
                iBounce = 0;
            }
        }
    }

    g_Bounces[ threadId ] = ( iBounce & 0x7FFFFFFF ) | ( isIdle ? 0x80000000 : 0 );
}

#endif

#if defined( FILL_INDIRECT_ARGUMENTS )

Buffer<uint> g_Counter        : register( t0 );
RWBuffer<uint> g_IndirectArgs : register( u0 );

[numthreads( 1, 1, 1 )]
void main()
{
    uint groupCount = g_Counter[ 0 ] / 32;
    if ( g_Counter[ 0 ] % 32 )
        groupCount++;
    groupCount = max( 1, groupCount );
    g_IndirectArgs[ 0 ] = groupCount;
    g_IndirectArgs[ 1 ] = 1;
    g_IndirectArgs[ 2 ] = 1;
}

#endif

#if defined( SET_IDLE )

cbuffer ControlConstants : register( b0 )
{
    float4 g_Background;
    uint g_PathCount;
    uint g_MaxBounceCount;
    uint2 g_BlockCounts;
    uint2 g_BlockDimension;
    uint2 g_FilmDimension;
}

RWStructuredBuffer<uint> g_Bounces : register( u0 );

[numthreads( 32, 1, 1 )]
void main( uint3 threadId : SV_DispatchThreadID )
{
    if ( threadId.x >= g_PathCount )
        return;

    g_Bounces[ threadId.x ] = 0x80000000;
}

#endif

#if defined( MEGAKERNEL )

cbuffer RayTracingConstants : register( b0 )
{
    row_major float4x4 g_CameraTransform;
    float2 g_FilmSize;
    uint2 g_Resolution;
    float4 g_Background;
    uint g_MaxBounceCount;
    uint g_PrimitiveCount;
    uint g_LightCount;
    float g_ApertureRadius;
    float g_FocalDistance;
    float g_FilmDistance;
    float2 g_BladeVertexPos;
    uint g_BladeCount;
    float g_ApertureBaseAngle;
    uint2 g_TileOffset;
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
StructuredBuffer<uint> g_MaterialIds                    : register( t14 );
StructuredBuffer<Material> g_Materials                  : register( t15 );
TextureCube<float3> g_EnvTexture                        : register( t16 );
RWTexture2D<float4> g_FilmTexture                       : register( u0 );

#include "BSDFs.inc.hlsl"

#define GROUP_SIZE_X 16
#define GROUP_SIZE_Y 16

[ numthreads( GROUP_SIZE_X, GROUP_SIZE_Y, 1 ) ]
void main( uint threadId : SV_GroupIndex, uint2 pixelPos : SV_DispatchThreadID )
{
    pixelPos += g_TileOffset;
    if ( any( pixelPos > g_Resolution ) )
        return;

    Xoshiro128StarStar rng = InitializeRandomNumberGenerator( pixelPos, g_FrameSeed );

    float3 pathThroughput = 1.0f;
    float3 l = 0.0f;
    float3 wi, wo;
    Intersection intersection;

    float2 pixelSample = GetNextSample2D( rng );
    float2 filmSample = ( pixelSample + pixelPos ) / g_Resolution;
    float3 apertureSample = GetNextSample3D( rng );
    GenerateRay( filmSample, apertureSample, g_FilmSize, g_ApertureRadius, g_FocalDistance, g_FilmDistance, g_BladeCount, g_BladeVertexPos, g_ApertureBaseAngle, g_CameraTransform, intersection.position, wi );

    float hitDistance;
    bool hasHit = IntersectScene( intersection.position, wi, threadId, g_Vertices, g_Triangles, g_BVHNodes, g_MaterialIds, g_Materials, g_PrimitiveCount, intersection, hitDistance );

    uint iBounce = 0;
    while ( iBounce <= g_MaxBounceCount )
    {
        if ( !hasHit )
        {
            l += pathThroughput * EnvironmentShader( wi, g_Background.xyz, g_EnvTexture, UVClampSampler );
            break;
        }

        wo = -wi;

        // Sample light
        bool isDeltaLight = false;
        uint lightIndex = 0;
        if ( g_LightCount != 0 )
        {
            float lightSelectionSample = GetNextSample1D( rng );
            float2 lightSamples = GetNextSample2D( rng );

            lightIndex = floor( lightSelectionSample * g_LightCount );
            SLightSampleResult sampleResult = SampleLight( g_Lights[ lightIndex ], intersection, lightSamples, wo );
            isDeltaLight = sampleResult.isDeltaLight;

            if ( any( sampleResult.radiance > 0.0f ) && sampleResult.pdf > 0.0f
                && !IsOcculuded( OffsetRayOrigin( intersection.position, intersection.geometryNormal, sampleResult.wi ), sampleResult.wi, sampleResult.distance, threadId, g_Vertices, g_Triangles, g_BVHNodes, g_PrimitiveCount ) )
            {
                float3 bsdf = EvaluateBSDF( sampleResult.wi, wo, intersection );
                float NdotWI = abs( dot( intersection.normal, sampleResult.wi ) );
                float bsdfPdf = EvaluateBSDFPdf( sampleResult.wi, wo, intersection );
                float weight = isDeltaLight ? 1.0f : PowerHeuristic( 1, sampleResult.pdf, 1, bsdfPdf );
                l += pathThroughput * sampleResult.radiance * bsdf * NdotWI * weight * g_LightCount / sampleResult.pdf;
            }
        }

        l += pathThroughput * intersection.emission;

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

            float3 lastHitPosition = intersection.position;
            hasHit = IntersectScene( OffsetRayOrigin( intersection.position, intersection.geometryNormal, wi ), wi, threadId, g_Vertices, g_Triangles, g_BVHNodes, g_MaterialIds, g_Materials, g_PrimitiveCount, intersection, hitDistance );

            if ( g_LightCount != 0 && !isDeltaLight )
            {
                float3 radiance;
                float lightPdf;
                float lightDistance = 0.0f;
                EvaluateLight( g_Lights[ lightIndex ], lastHitPosition, wi, lightDistance, radiance, lightPdf );
                if ( lightPdf > 0.0f && lightDistance < hitDistance ) // hitDistance is inf if there is no hit
                {
                    float weight = !isDeltaBxdf ? PowerHeuristic( 1, bsdfPdf, 1, lightPdf ) : 1.0f;
                    l += pathThroughput * radiance * weight * g_LightCount;
                }
            }
        }

        ++iBounce;
    }

    AddSampleToFilm( l, pixelSample, pixelPos, g_FilmTexture );
}

#endif

#if defined( OUTPUT_NORMAL ) || defined( OUTPUT_TANGENT ) || defined( OUTPUT_ALBEDO ) || defined( OUTPUT_NEGATIVE_NDOTV ) || defined( OUTPUT_BACKFACE )

cbuffer RayTracingConstants : register( b0 )
{
    row_major float4x4 g_CameraTransform;
    float2 g_FilmSize;
    uint2 g_Resolution;
    float4 g_Background;
    uint g_MaxBounceCount;
    uint g_PrimitiveCount;
    uint g_LightCount;
    float g_ApertureRadius;
    float g_FocalDistance;
    float g_FilmDistance;
    float2 g_BladeVertexPos;
    uint g_BladeCount;
    float g_ApertureBaseAngle;
    uint2 g_TileOffset;
}

StructuredBuffer<Vertex> g_Vertices     : register( t0 );
StructuredBuffer<uint> g_Triangles      : register( t1 );
StructuredBuffer<BVHNode> g_BVHNodes    : register( t13 );
StructuredBuffer<uint> g_MaterialIds    : register( t14 );
StructuredBuffer<Material> g_Materials  : register( t15 );
RWTexture2D<float4> g_FilmTexture       : register( u0 );

#define GROUP_SIZE_X 16
#define GROUP_SIZE_Y 16

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
    if ( IntersectScene( intersection.position, wo, threadId, g_Vertices, g_Triangles, g_BVHNodes, g_MaterialIds, g_Materials, g_PrimitiveCount, intersection, hitDistance ) )
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

    AddSampleToFilm( l, pixelSample, pixelPos, g_FilmTexture );
}

#endif