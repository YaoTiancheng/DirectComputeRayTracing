#include "RayTracingCommon.inc.hlsl"

struct SRay
{
    float3 origin;
    float  tMax;
    float3 direction;
    float  tMin;
};

struct SRayHit
{
    float t;
    float2 uv;
    uint triangleId;
    uint instanceIndex;
};

struct SPathAccumulation
{
    float3 pathThroughput;
    float bsdfPdf;
    float3 Li;
    bool isDeltaBxdf;
};

void UnpackPathFlags( uint flags, out bool isIdle, out bool hasShadowRayHit, out bool shouldTerminate, out uint bounce )
{
    isIdle = ( flags & 0x80000000 ) != 0;
    hasShadowRayHit = ( flags & 0x40000000 ) != 0;
    shouldTerminate = ( flags & 0x20000000 ) != 0;
    bounce = flags & 0xFF;
}

uint PackPathFlags( bool isIdle, bool hasShadowRayHit, bool shouldTerminate, uint bounce )
{
    uint flags = isIdle ? 0x80000000 : 0;
    flags |= hasShadowRayHit ? 0x40000000 : 0;
    flags |= shouldTerminate ? 0x20000000 : 0;
    flags |= bounce & 0xFF;
    return flags;
}

uint PathFlags_SetHasShadowRayHit( uint flags, bool value )
{
    flags = ( value ? 0x40000000 : 0 ) | ( flags & 0xBFFFFFFF );
    return flags;
}

uint PathFlags_SetShouldTerminate( uint flags )
{
    flags |= 0x20000000;
    return flags;
}

uint PathFlags_GetBounce( uint flags )
{
    return flags & 0xFF;
}

uint PathFlags_SetBounce( uint flags, uint bounce )
{
    return ( flags & 0xFFFFFF00 ) | ( bounce & 0xFF );
}

#if defined( EXTENSION_RAY_CAST )

cbuffer QueueConstants : register( b0 )
{
    uint g_RayCount;
    uint g_ShadowRayCount;
}

StructuredBuffer<Vertex> g_Vertices                 : register( t0 );
StructuredBuffer<uint> g_Triangles                  : register( t1 );
StructuredBuffer<BVHNode> g_BVHNodes                : register( t2 );
StructuredBuffer<SRay> g_Rays                       : register( t3 );
Buffer<uint> g_PathIndices                          : register( t4 );
StructuredBuffer<float4x3> g_InstanceInvTransforms  : register( t5 );
RWStructuredBuffer<SRayHit> g_RayHits               : register( u0 );

[numthreads( 32, 1, 1 )]
void main( uint threadId : SV_DispatchThreadID, uint gtid : SV_GroupThreadID )
{
    if ( threadId >= g_RayCount )
        return;

    uint pathIndex = g_PathIndices[ threadId ];
    SRay ray = g_Rays[ pathIndex ];
    SHitInfo hitInfo;
    uint iterationCounter;
    bool hasHit = BVHIntersectNoInterp( ray.origin
        , ray.direction
        , 0
        , gtid
        , g_Vertices
        , g_Triangles
        , g_BVHNodes
        , g_InstanceInvTransforms
        , hitInfo
        , iterationCounter );
    SRayHit rayHit;
    rayHit.t = hasHit ? hitInfo.t : FLT_INF;
    rayHit.uv = float2( hitInfo.u, hitInfo.v );
    rayHit.triangleId = ( hitInfo.triangleId & 0x7FFFFFFF ) | ( hitInfo.backface ? 1 << 31 : 0 );
    rayHit.instanceIndex = hitInfo.instanceIndex;

    g_RayHits[ pathIndex ] = rayHit;
}

#endif

#if defined( SHADOW_RAY_CAST )

cbuffer QueueConstants : register( b0 )
{
    uint g_RayCount;
    uint g_ShadowRayCount;
}

StructuredBuffer<Vertex> g_Vertices                 : register( t0 );
StructuredBuffer<uint> g_Triangles                  : register( t1 );
StructuredBuffer<BVHNode> g_BVHNodes                : register( t2 );
StructuredBuffer<SRay> g_Rays                       : register( t3 );
Buffer<uint> g_PathIndices                          : register( t4 );
StructuredBuffer<float4x3> g_InstanceInvTransforms  : register( t5 );
RWBuffer<uint> g_Flags                              : register( u0 );

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
        , g_InstanceInvTransforms );

    uint flags = PathFlags_SetHasShadowRayHit( g_Flags[ pathIndex ], hasHit );
    g_Flags[ pathIndex ] = flags;
}

#endif

#if defined( NEW_PATH )

cbuffer QueueConstants : register( b0 )
{
    uint g_MaterialRayCount;
    uint g_NewPathRayCount;
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
RWBuffer<float4> g_LightSamplingResults         : register( u2 );
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
    g_LightSamplingResults[ pathIndex ] = 0.f;
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
    uint g_MaterialRayCount;
    uint g_NewPathRayCount;
}

cbuffer MaterialConstants : register( b1 )
{
    uint g_LightCount;
    uint g_MaxBounceCount;
    uint g_EnvironmentLightIndex;
}

Buffer<uint> g_PathIndices                              : register( t0 );
StructuredBuffer<SRayHit> g_RayHits                     : register( t1 );
StructuredBuffer<Vertex> g_Vertices                     : register( t2 );
StructuredBuffer<uint> g_Triangles                      : register( t3 );
StructuredBuffer<SLight> g_Lights                       : register( t4 );
StructuredBuffer<float4x3> g_InstanceTransforms         : register( t5 );
StructuredBuffer<uint> g_MaterialIds                    : register( t6 );
StructuredBuffer<Material> g_Materials                  : register( t7 );
Buffer<uint> g_InstanceLightIndices                     : register( t8 );
Texture2D<float> g_BRDFTexture                          : register( t9 );
Texture2D<float> g_BRDFAvgTexture                       : register( t10 );
Texture2DArray<float> g_BRDFDielectricTexture           : register( t11 );
Texture2DArray<float> g_BSDFTexture                     : register( t12 );
Texture2DArray<float> g_BSDFAvgTexture                  : register( t13 );
TextureCube<float3> g_EnvTexture                        : register( t14 );

RWStructuredBuffer<SRay> g_Rays                         : register( u0 );
RWStructuredBuffer<SRay> g_ShadowRays                   : register( u1 );
RWBuffer<uint> g_Flags                                  : register( u2 );
RWStructuredBuffer<Xoshiro128StarStar> g_Rngs           : register( u3 );
RWBuffer<float4> g_LightSamplingResults                 : register( u4 );
RWStructuredBuffer<SPathAccumulation> g_PathAccumulation : register( u5 );
RWBuffer<uint> g_RayCounters                            : register( u6 );
RWBuffer<uint> g_ExtensionRayQueue                      : register( u7 );
RWBuffer<uint> g_ShadowRayQueue                         : register( u8 );

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
    hitInfo.instanceIndex = rayHit.instanceIndex;

    SRay ray = g_Rays[ pathIndex ];
    float3 origin = ray.origin;
    float3 direction = ray.direction;
    Intersection intersection;
    HitInfoToIntersection( origin, direction, hitInfo, g_Vertices, g_Triangles, g_MaterialIds, g_Materials, g_InstanceTransforms, g_InstanceLightIndices, intersection );

    Xoshiro128StarStar rng = g_Rngs[ pathIndex ];
    SPathAccumulation pathAccumulation = g_PathAccumulation[ pathIndex ];

    uint pathFlags = g_Flags[pathIndex];
    uint bounce = PathFlags_GetBounce(pathFlags);

    // Evaluate light
    {
        uint lightIndex = hitInfo.t != FLT_INF ? intersection.lightIndex : g_EnvironmentLightIndex;
#if defined( LIGHT_VISIBLE )
        if ( lightIndex != LIGHT_INDEX_INVALID )
#else
        if ( bounce > 0 && lightIndex != LIGHT_INDEX_INVALID )
#endif
        {
            float3 radiance;
            float lightPdf;
            EvaluateLightDirect( lightIndex, intersection.triangleIndex, intersection.geometryNormal, direction, hitInfo.t, g_Lights, g_LightCount, g_Vertices, g_Triangles, g_InstanceTransforms, g_EnvTexture, UVClampSampler, radiance, lightPdf );
            if ( lightPdf > 0.0f )
            {
                float weight = !pathAccumulation.isDeltaBxdf ? PowerHeuristic( 1, pathAccumulation.bsdfPdf, 1, lightPdf ) : 1.0f;
                pathAccumulation.Li += pathAccumulation.pathThroughput * radiance * weight;
            }
        }
    }

    float3 lightSamplingResult = 0.0f;
    bool shouldTerminate = false;
    bool hasShadowRay = false;

    if ( bounce > g_MaxBounceCount || hitInfo.t == FLT_INF )
    {
        pathFlags = PathFlags_SetShouldTerminate( pathFlags );
        shouldTerminate = true;
    }
    else
    {
        float3 wo = -direction;

        // Sample light
        if ( g_LightCount != 0 )
        {
            SLightSampleResult sampleResult = SampleLightDirect( intersection.position, g_Lights, g_LightCount, g_Vertices, g_Triangles, g_InstanceTransforms, g_EnvTexture, UVClampSampler, rng );
            bool isDeltaLight = sampleResult.isDeltaLight;
            if ( any( sampleResult.radiance > 0.0f ) && sampleResult.pdf > 0.0f )
            {
                float3 bsdf = EvaluateBSDF( sampleResult.wi, wo, intersection );
                float NdotWI = abs( dot( intersection.normal, sampleResult.wi ) );
                float bsdfPdf = EvaluateBSDFPdf( sampleResult.wi, wo, intersection );
                float weight = isDeltaLight ? 1.0f : PowerHeuristic( 1, sampleResult.pdf, 1, bsdfPdf );
                lightSamplingResult = pathAccumulation.pathThroughput * sampleResult.radiance * bsdf * NdotWI * weight / sampleResult.pdf;

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
        float bsdfPdf = 0.f;
        bool isDeltaBxdf = false;
        {
            float bsdfSelectionSample = GetNextSample1D( rng );
            float2 bsdfSample = GetNextSample2D( rng );

            float3 wi;
            float3 bsdf;
            SampleBSDF( wo, bsdfSample, bsdfSelectionSample, intersection, wi, bsdf, bsdfPdf, isDeltaBxdf );

            if ( any( bsdf != 0.0f ) && bsdfPdf != 0.0f )
            {
                float NdotWI = abs( dot( intersection.normal, wi ) );
                pathAccumulation.pathThroughput = pathAccumulation.pathThroughput * bsdf * NdotWI / bsdfPdf;

                ray.direction = wi;
                ray.origin = OffsetRayOrigin( intersection.position, intersection.geometryNormal, wi );
                ray.tMin = 0.0f;
                ray.tMax = FLT_INF;
                g_Rays[ pathIndex ] = ray;

                pathFlags = PathFlags_SetBounce( pathFlags, bounce + 1 );
            }
            else
            {
                pathFlags = PathFlags_SetShouldTerminate( pathFlags );
                shouldTerminate = true;
            }
        }
        pathAccumulation.bsdfPdf = bsdfPdf;
        pathAccumulation.isDeltaBxdf = isDeltaBxdf;
    }

    // If no shadow ray is spawned then set HasShadowRayHit to false so the control kernel will not load the light sampling result.
    if ( !hasShadowRay )
    {
        pathFlags = PathFlags_SetHasShadowRayHit( pathFlags, false );
    }

    g_Flags[ pathIndex ] = pathFlags;
    g_Rngs[ pathIndex ] = rng;
    g_PathAccumulation[ pathIndex ] = pathAccumulation;
    g_LightSamplingResults[ pathIndex ] = float4( lightSamplingResult, 0.f );

    uint laneIndex = WaveGetLaneIndex( gtid );
    // Write extension ray indices
    {
        uint rayMask = WaveActiveBallot32( laneIndex, !shouldTerminate );
        uint rayCount = countbits( rayMask );
        uint rayIndexBase = 0;
        if ( laneIndex == 0 )
        {
            InterlockedAdd( g_RayCounters[ 0 ], rayCount, rayIndexBase );
        }

        rayIndexBase = WaveReadLaneFirst32( laneIndex, rayIndexBase );
        uint rayIndexOffset = WavePrefixCountBits32( laneIndex, rayMask );
        if ( !shouldTerminate )
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
    float4 g_Background;
    uint g_PathCount;
    uint g_MaxBounceCount;
    uint2 g_BlockCounts;
    uint2 g_BlockDimension;
    uint2 g_FilmDimension;
}

StructuredBuffer<float2> g_PixelSamples     : register( t0 );
Buffer<float4> g_LightSamplingResults       : register( t1 );

RWStructuredBuffer<uint2> g_PixelPositions  : register( u0 );
RWStructuredBuffer<SPathAccumulation> g_PathAccumulation : register( u1 );
RWBuffer<uint> g_Flags                      : register( u2 );
RWBuffer<uint> g_QueueCounters              : register( u3 );
RWBuffer<uint> g_MaterialQueue              : register( u4 );
RWBuffer<uint> g_NewPathQueue               : register( u5 );
RWBuffer<uint> g_NextBlockIndex             : register( u6 );
RWTexture2D<float2> g_SamplePositionTexture : register( u7 );
RWTexture2D<float3> g_SampleValueTexture    : register( u8 );

[numthreads( 32, 1, 1 )]
void main( uint threadId : SV_DispatchThreadID, uint gtid : SV_GroupThreadID )
{
    bool isIdle = false;
    bool shouldTerminate = false;
    bool hasShadowRayHit = false;
    uint bounce = 0;
    UnpackPathFlags( g_Flags[ threadId ], isIdle, hasShadowRayHit, shouldTerminate, bounce );
    isIdle = isIdle || threadId >= g_PathCount;

    if ( !isIdle )
    {
        SPathAccumulation pathAccumulation = g_PathAccumulation[ threadId ];

        float4 lightSamplingResults = g_LightSamplingResults[ threadId ];
        pathAccumulation.Li += !hasShadowRayHit ? lightSamplingResults.xyz : 0.0f;

        if ( shouldTerminate )
        {
            uint2 pixelPosition = g_PixelPositions[ threadId ];
            float2 pixelSample = g_PixelSamples[ threadId ];
            WriteSample( pathAccumulation.Li, pixelSample, pixelPosition, g_SamplePositionTexture, g_SampleValueTexture );
            isIdle = true;
        }
        else
        {
            g_PathAccumulation[ threadId ] = pathAccumulation;
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
                pathAccumulation.bsdfPdf = 0.f;
                pathAccumulation.isDeltaBxdf = true;
                g_PathAccumulation[ threadId ] = pathAccumulation;
                g_NewPathQueue[ rayIndexBase + rayIndexOffset ] = threadId;
                isIdle = false;
                bounce = 0;
            }
        }
    }

    g_Flags[ threadId ] = PackPathFlags( isIdle, false, false, bounce );
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

RWBuffer<uint> g_Flags : register( u0 );

[numthreads( 32, 1, 1 )]
void main( uint3 threadId : SV_DispatchThreadID )
{
    if ( threadId.x >= g_PathCount )
        return;

    g_Flags[ threadId.x ] = PackPathFlags( true, false, false, 0 );
}

#endif