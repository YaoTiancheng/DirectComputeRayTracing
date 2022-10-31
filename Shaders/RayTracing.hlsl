
#define FLT_INF asfloat( 0x7f800000 )
#define SHADOW_EPSILON 1e-3f

#include "Math.inc.hlsl"
#include "MonteCarlo.inc.hlsl"
#include "Vertex.inc.hlsl"
#include "LightSharedDef.inc.hlsl"
#include "Light.inc.hlsl"
#include "Material.inc.hlsl"
#include "BVHNode.inc.hlsl"
#include "Xoshiro.inc.hlsl"

#include "Samples.inc.hlsl"
#include "BVHAccel.inc.hlsl"
#include "HitShader.inc.hlsl"
#include "EnvironmentShader.inc.hlsl"
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
    , StructuredBuffer<float4x3> instances
    , Buffer<uint> instanceLightIndices
    , inout Intersection intersection )
{
    intersection.lightIndex = instanceLightIndices[ hitInfo.instanceIndex ];
    intersection.triangleIndex = hitInfo.triangleId;

    Vertex v0 = vertices[ triangles[ hitInfo.triangleId * 3 ] ];
    Vertex v1 = vertices[ triangles[ hitInfo.triangleId * 3 + 1 ] ];
    Vertex v2 = vertices[ triangles[ hitInfo.triangleId * 3 + 2 ] ];
    HitShader( origin, direction, v0, v1, v2, hitInfo.t, hitInfo.u, hitInfo.v, hitInfo.triangleId, hitInfo.backface, materialIds, materials, intersection );
    // Transform the position & vectors from local space to world space. Assuming the transform only contains uniform scaling otherwise the transformed vectors are wrong.
    intersection.position = mul( float4( intersection.position, 1.f ), instances[ hitInfo.instanceIndex ] );
    intersection.normal = normalize( mul( float4( intersection.normal, 0.f ), instances[ hitInfo.instanceIndex ] ) );
    intersection.geometryNormal = normalize( mul( float4( intersection.geometryNormal, 0.f ), instances[ hitInfo.instanceIndex ] ) );
    intersection.tangent = normalize( mul( float4( intersection.tangent, 0.f ), instances[ hitInfo.instanceIndex ] ) );
}

bool IntersectScene( float3 origin
    , float3 direction
    , uint dispatchThreadIndex
    , StructuredBuffer<Vertex> vertices
    , StructuredBuffer<uint> triangles
    , StructuredBuffer<BVHNode> BVHNodes
    , StructuredBuffer<float4x3> instancesTransforms
    , StructuredBuffer<float4x3> instancesInvTransforms
    , Buffer<uint> instanceLightIndices
    , StructuredBuffer<uint> materialIds
    , StructuredBuffer<Material> materials
    , inout Intersection intersection
    , out float t
    , out uint iterationCounter )
{
    intersection.lightIndex = LIGHT_INDEX_INVALID;
    intersection.triangleIndex = 0;
    SHitInfo hitInfo = (SHitInfo)0;
    t = FLT_INF;
    bool hasIntersection = BVHIntersectNoInterp( origin, direction, 0, dispatchThreadIndex, vertices, triangles, BVHNodes, instancesInvTransforms, hitInfo, iterationCounter );
    if ( hasIntersection )
    {
        t = hitInfo.t;
        HitInfoToIntersection( origin, direction, hitInfo, vertices, triangles, materialIds, materials, instancesTransforms, instanceLightIndices, intersection );
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
    , StructuredBuffer<float4x3> Instances )
{
    return BVHIntersect( origin, direction, 0, distance, dispatchThreadIndex, vertices, triangles, BVHNodes, Instances );
}

void WriteSample( float3 l, float2 sample, uint2 pixelPos, RWTexture2D<float2> samplePositionTexture, RWTexture2D<float3> sampleValueTexture )
{
    samplePositionTexture[ pixelPos ] = sample;
    sampleValueTexture[ pixelPos ] = l;
}

struct SLightSampleResult
{
    float3 radiance;
    float3 wi;
    float pdf;
    float distance;
    bool isDeltaLight;
};

SLightSampleResult SampleLightDirect( float3 p, StructuredBuffer<SLight> lights, uint lightCount, StructuredBuffer<Vertex> vertices, StructuredBuffer<uint> triangles, StructuredBuffer<float4x3> instanceTransforms, inout Xoshiro128StarStar rng )
{
    SLightSampleResult result;

    // Uniformly select one light
    float lightSelectionSample = GetNextSample1D( rng );
    uint lightIndex = floor( lightSelectionSample * lightCount );
    SLight light = lights[ lightIndex ];

    result.isDeltaLight = false;
    if ( light.flags & LIGHT_FLAGS_POINT_LIGHT )
    {
        PointLight_Sample( light, p, result.radiance, result.wi, result.distance, result.pdf );
        result.isDeltaLight = true;
    }
    else if ( light.flags & LIGHT_FLAGS_MESH_LIGHT )
    {
        // Uniformly select one triangle
        float triangleSelectionSample = GetNextSample1D( rng );
        float2 triangleSample = GetNextSample2D( rng );
        uint triangleIndex = Light_GetTriangleOffset( light ) + floor( triangleSelectionSample * Light_GetTriangleCount( light ) );

        float3 v0 = vertices[ triangles[ triangleIndex * 3 ] ].position;
        float3 v1 = vertices[ triangles[ triangleIndex * 3 + 1 ] ].position;
        float3 v2 = vertices[ triangles[ triangleIndex * 3 + 2 ] ].position;
        float4x3 instanceTransform = instanceTransforms[ Light_GetInstanceIndex( light ) ];
        TriangleLight_Sample( light, instanceTransform, v0, v1, v2, triangleSample, p, result.radiance, result.wi, result.distance, result.pdf );

        result.pdf /= Light_GetTriangleCount( light );
        result.radiance *= Light_GetTriangleCount( light );
    }

    result.pdf /= lightCount;
    result.radiance *= lightCount;

    result.distance *= 1 - SHADOW_EPSILON;
    return result;
}

void EvaluateLightDirect( uint lightIndex, uint triangleIndex, float3 normal, float3 wi, float distance, StructuredBuffer<SLight> lights, uint lightCount
    , StructuredBuffer<Vertex> vertices, StructuredBuffer<uint> triangles, StructuredBuffer<float4x3> instanceTransforms, out float3 radiance, out float pdf )
{
    radiance = 0.f;
    pdf = 0.f;

    SLight light = lights[ lightIndex ];
    if ( light.flags & LIGHT_FLAGS_MESH_LIGHT )
    {
        float3 v0 = vertices[ triangles[ triangleIndex * 3 ] ].position;
        float3 v1 = vertices[ triangles[ triangleIndex * 3 + 1 ] ].position;
        float3 v2 = vertices[ triangles[ triangleIndex * 3 + 2 ] ].position;
        float4x3 instanceTransform = instanceTransforms[ Light_GetInstanceIndex( light ) ];
        TriangleLight_EvaluateWithPDF( light, instanceTransform, v0, v1, v2, wi, normal, distance, radiance, pdf );
        pdf /= Light_GetTriangleCount( light );
    }
    
    pdf /= lightCount;
}

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
    uint               g_MaterialRayCount;
    uint               g_NewPathRayCount;
}

cbuffer MaterialConstants : register( b1 )
{
    uint               g_LightCount;
    uint               g_MaxBounceCount;
}

Buffer<uint>                                g_PathIndices                       : register( t0 );
StructuredBuffer<SRayHit>                   g_RayHits                           : register( t1 );
StructuredBuffer<Vertex>                    g_Vertices                          : register( t2 );
StructuredBuffer<uint>                      g_Triangles                         : register( t3 );
StructuredBuffer<SLight>                    g_Lights                            : register( t4 );
StructuredBuffer<float4x3>                  g_InstanceTransforms                : register( t5 );
StructuredBuffer<uint>                      g_MaterialIds                       : register( t6 );
StructuredBuffer<Material>                  g_Materials                         : register( t7 );
Buffer<uint>                                g_InstanceLightIndices              : register( t8 );
Texture2D<float>                            g_CookTorranceCompETexture          : register( t9 );
Texture2D<float>                            g_CookTorranceCompEAvgTexture       : register( t10 );
Texture2D<float>                            g_CookTorranceCompInvCDFTexture     : register( t11 );
Texture2D<float>                            g_CookTorranceCompPdfScaleTexture   : register( t12 );
Texture2DArray<float>                       g_CookTorranceCompEFresnelTexture   : register( t13 );
Texture2DArray<float>                       g_CookTorranceBSDFETexture          : register( t14 );
Texture2DArray<float>                       g_CookTorranceBSDFAvgETexture       : register( t15 );
Texture2DArray<float>                       g_CookTorranceBTDFETexture          : register( t16 );
Texture2DArray<float>                       g_CookTorranceBSDFInvCDFTexture     : register( t17 );
Texture2DArray<float>                       g_CookTorranceBSDFPDFScaleTexture   : register( t18 );

RWStructuredBuffer<SRay>                    g_Rays                              : register( u0 );
RWStructuredBuffer<SRay>                    g_ShadowRays                        : register( u1 );
RWBuffer<uint>                              g_Flags                             : register( u2 );
RWStructuredBuffer<Xoshiro128StarStar>      g_Rngs                              : register( u3 );
RWBuffer<float4>                            g_LightSamplingResults              : register( u4 );
RWStructuredBuffer<SPathAccumulation>       g_PathAccumulation                  : register( u5 );
RWBuffer<uint>                              g_RayCounters                       : register( u6 );
RWBuffer<uint>                              g_ExtensionRayQueue                 : register( u7 );
RWBuffer<uint>                              g_ShadowRayQueue                    : register( u8 );

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
#if defined( LIGHT_VISIBLE )
    if ( intersection.lightIndex != LIGHT_INDEX_INVALID )
#else
    if ( bounce > 0 && intersection.lightIndex != LIGHT_INDEX_INVALID )
#endif
    {
        float3 radiance;
        float lightPdf;
        EvaluateLightDirect( intersection.lightIndex, intersection.triangleIndex, intersection.geometryNormal, direction, hitInfo.t, g_Lights, g_LightCount, g_Vertices, g_Triangles, g_InstanceTransforms, radiance, lightPdf );
        if ( lightPdf > 0.0f )
        {
            float weight = !pathAccumulation.isDeltaBxdf ? PowerHeuristic( 1, pathAccumulation.bsdfPdf, 1, lightPdf ) : 1.0f;
            pathAccumulation.Li += pathAccumulation.pathThroughput * radiance * weight;
        }
    }

    float3 lightSamplingResult = 0.0f;
    bool shouldTerminate = false;
    bool hasShadowRay = false;

    if ( bounce > g_MaxBounceCount || hitInfo.t == FLT_INF )
    {
        g_Flags[ pathIndex ] = PathFlags_SetShouldTerminate( pathFlags );
        shouldTerminate = true;
    }
    else
    {
        float3 wo = -direction;

        // Sample light
        if ( g_LightCount != 0 )
        {
            SLightSampleResult sampleResult = SampleLightDirect( intersection.position, g_Lights, g_LightCount, g_Vertices, g_Triangles, g_InstanceTransforms, rng );
            bool isDeltaLight = sampleResult.isDeltaLight;
            if ( any( sampleResult.radiance > 0.0f ) && sampleResult.pdf > 0.0f )
            {
                float3 bsdf = EvaluateBSDF( sampleResult.wi, wo, intersection );
                float NdotWI = abs( dot( intersection.normal, sampleResult.wi ) );
                float bsdfPdf = EvaluateBSDFPdf( sampleResult.wi, wo, intersection );
                float weight = isDeltaLight ? 1.0f : PowerHeuristic( 1, sampleResult.pdf, 1, bsdfPdf );
                lightSamplingResult = pathAccumulation.pathThroughput * sampleResult.radiance * bsdf * NdotWI * weight;

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

                g_Flags[ pathIndex ] = PathFlags_SetBounce( pathFlags, bounce + 1 );
            }
            else
            {
                g_Flags[ pathIndex ] = PathFlags_SetShouldTerminate( pathFlags );
                shouldTerminate = true;
            }
        }
        pathAccumulation.bsdfPdf = bsdfPdf;
        pathAccumulation.isDeltaBxdf = isDeltaBxdf;
    }

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
    float4             g_Background;
    uint               g_PathCount;
    uint               g_MaxBounceCount;
    uint2              g_BlockCounts;
    uint2              g_BlockDimension;
    uint2              g_FilmDimension;
}

StructuredBuffer<float2>        g_PixelSamples      : register( t0 );
Buffer<float4>                  g_LightSamplingResults : register( t1 );

RWStructuredBuffer<uint2>       g_PixelPositions    : register( u0 );
RWStructuredBuffer<SPathAccumulation> g_PathAccumulation : register( u1 );
RWBuffer<uint>                  g_Flags             : register( u2 );
RWBuffer<uint>                  g_QueueCounters     : register( u3 );
RWBuffer<uint>                  g_MaterialQueue     : register( u4 );
RWBuffer<uint>                  g_NewPathQueue      : register( u5 );
RWBuffer<uint>                  g_NextBlockIndex    : register( u6 );
RWTexture2D<float2>             g_SamplePositionTexture : register( u7 );
RWTexture2D<float3>             g_SampleValueTexture : register( u8 );

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

#if defined( MEGAKERNEL )

cbuffer RayTracingConstants : register( b0 )
{
    row_major float4x4 g_CameraTransform;
    float2 g_FilmSize;
    uint2 g_Resolution;
    float4 g_Background;
    uint g_MaxBounceCount;
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
                SLightSampleResult sampleResult = SampleLightDirect( intersection.position, g_Lights, g_LightCount, g_Vertices, g_Triangles, g_InstanceTransforms, rng );
                bool isDeltaLight = sampleResult.isDeltaLight;
                if ( any( sampleResult.radiance > 0.f ) && sampleResult.pdf > 0.f
                    && !IsOcculuded( OffsetRayOrigin( intersection.position, intersection.geometryNormal, sampleResult.wi ), sampleResult.wi, sampleResult.distance, threadId, g_Vertices, g_Triangles, g_BVHNodes, g_InstanceInvTransforms ) )
                {
                    float3 bsdf = EvaluateBSDF( sampleResult.wi, wo, intersection );
                    float NdotWI = abs( dot( intersection.normal, sampleResult.wi ) );
                    float bsdfPdf = EvaluateBSDFPdf( sampleResult.wi, wo, intersection );
                    float weight = isDeltaLight ? 1.0f : PowerHeuristic( 1, sampleResult.pdf, 1, bsdfPdf );
                    l += pathThroughput * sampleResult.radiance * bsdf * NdotWI * weight;
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

                if ( intersection.lightIndex != LIGHT_INDEX_INVALID )
                {
                    float3 radiance;
                    float lightPdf;
                    EvaluateLightDirect( intersection.lightIndex, intersection.triangleIndex, intersection.geometryNormal, wi, hitDistance, g_Lights, g_LightCount, g_Vertices, g_Triangles, g_InstanceTransforms, radiance, lightPdf );
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
    else
    {
        l = pathThroughput * EnvironmentShader( wi, g_Background.xyz, g_EnvTexture, UVClampSampler );
    }

    WriteSample( l, pixelSample, pixelPos, g_SamplePositionTexture, g_SampleValueTexture );
}

#endif

#if defined( OUTPUT_NORMAL ) || defined( OUTPUT_TANGENT ) || defined( OUTPUT_ALBEDO ) || defined( OUTPUT_NEGATIVE_NDOTV ) || defined( OUTPUT_BACKFACE ) || defined( OUTPUT_ITERATION_COUNT )

cbuffer RayTracingConstants : register( b0 )
{
    row_major float4x4 g_CameraTransform;
    float2 g_FilmSize;
    uint2 g_Resolution;
    float4 g_Background;
    uint g_MaxBounceCount;
    uint g_LightCount;
    float g_ApertureRadius;
    float g_FocalDistance;
    float g_FilmDistance;
    float2 g_BladeVertexPos;
    uint g_BladeCount;
    float g_ApertureBaseAngle;
    uint2 g_TileOffset;
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