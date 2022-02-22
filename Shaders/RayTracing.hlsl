
cbuffer RayTracingConstants : register( b0 )
{
    row_major float4x4  g_CameraTransform;
    float2              g_FilmSize;
    uint2               g_Resolution;
    float4              g_Background;
    uint                g_MaxBounceCount;
    uint                g_PrimitiveCount;
    uint                g_LightCount;
    float               g_ApertureRadius;
    float               g_FocalDistance;
    float               g_FilmDistance;
    float2              g_BladeVertexPos;
    uint                g_BladeCount;
    float               g_ApertureBaseAngle;
}

cbuffer RayTracingFrameConstants : register( b1 )
{
    uint                g_FrameSeed;
    uint2               g_TileOffset;
}

#include "Vertex.inc.hlsl"
#include "Light.inc.hlsl"
#include "Material.inc.hlsl"
#include "BVHNode.inc.hlsl"
#include "Xoshiro.inc.hlsl"

StructuredBuffer<Vertex>                g_Vertices                          : register( t0 );
StructuredBuffer<uint>                  g_Triangles                         : register( t1 );
StructuredBuffer<SLight>                g_Lights                            : register( t2 );
Texture2D<float>                        g_CookTorranceCompETexture          : register( t3 );
Texture2D<float>                        g_CookTorranceCompEAvgTexture       : register( t4 );
Texture2D<float>                        g_CookTorranceCompInvCDFTexture     : register( t5 );
Texture2D<float>                        g_CookTorranceCompPdfScaleTexture   : register( t6 );
Texture2DArray<float>                   g_CookTorranceCompEFresnelTexture   : register( t7 );
Texture2DArray<float>                   g_CookTorranceBSDFETexture          : register( t8 );
Texture2DArray<float>                   g_CookTorranceBSDFAvgETexture       : register( t9 );
Texture2DArray<float>                   g_CookTorranceBTDFETexture          : register( t10 );
Texture2DArray<float>                   g_CookTorranceBSDFInvCDFTexture     : register( t11 );
Texture2DArray<float>                   g_CookTorranceBSDFPDFScaleTexture   : register( t12 );
StructuredBuffer<BVHNode>               g_BVHNodes                          : register( t13 );
StructuredBuffer<uint>                  g_MaterialIds                       : register( t14 );
StructuredBuffer<Material>              g_Materials                         : register( t15 );
TextureCube<float3>                     g_EnvTexture                        : register( t16 );
RWTexture2D<float4>                     g_FilmTexture                       : register( u0 );

SamplerState UVClampSampler;

#include "Samples.inc.hlsl"
#include "BSDFs.inc.hlsl"
#include "BVHAccel.inc.hlsl"
#include "HitShader.inc.hlsl"
#include "EnvironmentShader.inc.hlsl"
#include "LightSampling.inc.hlsl"

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

bool IntersectScene( float3 origin
	, float3 direction
    , uint dispatchThreadIndex
	, out Intersection intersection )
{
    return BVHIntersect( origin, direction, 0, dispatchThreadIndex, intersection );
}

bool IsOcculuded( float3 origin
    , float3 direction
    , float distance
    , uint dispatchThreadIndex )
{
    return BVHIntersect( origin, direction, 0, distance, dispatchThreadIndex );
}

void AddSampleToFilm( float3 l
    , float2 sample
    , uint2 pixelPos )
{
    float4 c = g_FilmTexture[ pixelPos ];
    c += float4( l, 1.0f );
    g_FilmTexture[ pixelPos ] = c;
}

struct SLightSamples
{
    float  lightSelectionSample;
    float2 lightSample;
#if defined( MULTIPLE_IMPORTANCE_SAMPLING )
    float  bsdfSelectionSample;
    float2 bsdfSample;
#endif
};

float3 EstimateDirect( SLight light, Intersection intersection, SLightSamples samples, float3 wo, uint dispatchThreadIndex )
{
#if defined( MULTIPLE_IMPORTANCE_SAMPLING )
    float3 l, wi, result = 0.0f;
    float distance, lightPdf;
    bool isDeltaLight = false;
    if ( light.flags & LIGHT_FLAGS_POINT_LIGHT )
    {
        SamplePointLight( light, samples.lightSample, intersection.position, l, wi, distance, lightPdf );
        isDeltaLight = true;
    }
    else
    {
        SampleRectangleLight( light, samples.lightSample, intersection.position, l, wi, distance, lightPdf );
    }

    if ( any( l > 0.0f ) && lightPdf > 0.0f && !IsOcculuded( OffsetRayOrigin( intersection.position, intersection.geometryNormal, wi ), wi, distance, dispatchThreadIndex ) )
    {
        float3 bsdf = EvaluateBSDF( wi, wo, intersection );
        float NdotWI = abs( dot( intersection.normal, wi ) );
        float bsdfPdf = EvaluateBSDFPdf( wi, wo, intersection );
        float weight = isDeltaLight ? 1.0f : PowerHeuristic( 1, lightPdf, 1, bsdfPdf );

        result = l * bsdf * NdotWI * weight / lightPdf;
    }

    if ( !isDeltaLight )
    {
        float3 bsdf;
        float bsdfPdf;
        bool isDeltaBxdf;
        SampleBSDF( wo, samples.bsdfSample, samples.bsdfSelectionSample, intersection, wi, bsdf, bsdfPdf, isDeltaBxdf );

        if ( all( bsdf == 0.0f ) || bsdfPdf == 0.0f )
            return result;

        lightPdf = EvaluateRectangleLightPdf( light, wi, intersection.position, distance );
        if ( lightPdf == 0.0f )
            return result;

        float weight = !isDeltaBxdf ? PowerHeuristic( 1, bsdfPdf, 1, lightPdf ) : 1.0f;

        l = 0.0f;
        if ( !IsOcculuded( OffsetRayOrigin( intersection.position, intersection.geometryNormal, wi ), wi, distance, dispatchThreadIndex ) )
        {
            l = light.color;
        }

        float NdotWI = abs( dot( intersection.normal, wi ) );
        result += l * bsdf * NdotWI * weight / bsdfPdf;
    }

    return result;
#else
    float3 l, wi;
    float distance, pdf;
    if ( light.flags & LIGHT_FLAGS_POINT_LIGHT )
        SamplePointLight( light, samples.lightSample, intersection.position, l, wi, distance, pdf );
    else
        SampleRectangleLight( light, samples.lightSample, intersection.position, l, wi, distance, pdf );

    if ( any( l > 0.0f ) && pdf > 0.0f && !IsOcculuded( OffsetRayOrigin( intersection.position, intersection.geometryNormal, wi ), wi, distance, dispatchThreadIndex ) )
    {
        float3 brdf = EvaluateBSDF( wi, wo, intersection );
        float NdotL = abs( dot( intersection.normal, wi ) );
        return l * brdf * NdotL / pdf;
    }

    return 0.0f;
#endif
}

void EstimateDirectNoOcclusion( SLight light, Intersection intersection, SLightSamples samples, float3 wo, out float3 lightSamplingResult, out float3 bsdfSamplingResult )
{
    lightSamplingResult = 0.f;
    bsdfSamplingResult = 0.f;

#if defined( MULTIPLE_IMPORTANCE_SAMPLING )
    float3 l, wi, result = 0.0f;
    float distance, lightPdf;
    bool isDeltaLight = false;
    if ( light.flags & LIGHT_FLAGS_POINT_LIGHT )
    {
        SamplePointLight( light, samples.lightSample, intersection.position, l, wi, distance, lightPdf );
        isDeltaLight = true;
    }
    else
    {
        SampleRectangleLight( light, samples.lightSample, intersection.position, l, wi, distance, lightPdf );
    }

    if ( any( l > 0.0f ) && lightPdf > 0.0f )
    {
        float3 bsdf = EvaluateBSDF( wi, wo, intersection );
        float NdotWI = abs( dot( intersection.normal, wi ) );
        float bsdfPdf = EvaluateBSDFPdf( wi, wo, intersection );
        float weight = isDeltaLight ? 1.0f : PowerHeuristic( 1, lightPdf, 1, bsdfPdf );

        lightSamplingResult = l * bsdf * NdotWI * weight / lightPdf;
    }

    if ( !isDeltaLight )
    {
        float3 bsdf;
        float bsdfPdf;
        bool isDeltaBxdf;
        SampleBSDF( wo, samples.bsdfSample, samples.bsdfSelectionSample, intersection, wi, bsdf, bsdfPdf, isDeltaBxdf );

        if ( all( bsdf == 0.0f ) || bsdfPdf == 0.0f )
            return result;

        lightPdf = EvaluateRectangleLightPdf( light, wi, intersection.position, distance );
        if ( lightPdf == 0.0f )
            return result;

        float weight = !isDeltaBxdf ? PowerHeuristic( 1, bsdfPdf, 1, lightPdf ) : 1.0f;

        l = light.color;

        float NdotWI = abs( dot( intersection.normal, wi ) );
        bsdfSamplingResult = l * bsdf * NdotWI * weight / bsdfPdf;
    }
#else
    float3 l, wi;
    float distance, pdf;
    if ( light.flags & LIGHT_FLAGS_POINT_LIGHT )
        SamplePointLight( light, samples.lightSample, intersection.position, l, wi, distance, pdf );
    else
        SampleRectangleLight( light, samples.lightSample, intersection.position, l, wi, distance, pdf );

    if ( any( l > 0.0f ) && pdf > 0.0f && !IsOcculuded( OffsetRayOrigin( intersection.position, intersection.geometryNormal, wi ), wi, distance, dispatchThreadIndex ) )
    {
        float3 brdf = EvaluateBSDF( wi, wo, intersection );
        float NdotL = abs( dot( intersection.normal, wi ) );
        lightSamplingResult = l * brdf * NdotL / pdf;
    }
#endif
}

float3 UniformSampleOneLight( SLightSamples samples, Intersection intersection, float3 wo, uint dispatchThreadIndex )
{
    uint lightIndex = floor( samples.lightSelectionSample * g_LightCount );
    return g_LightCount != 0 ? EstimateDirect( g_Lights[ lightIndex ], intersection, samples, wo, dispatchThreadIndex ) * g_LightCount : 0.0f;
}

struct SRayQuery
{
    float3 origin;
    float3 direction;
    float  tMin;
};

struct SRayOcclusionQuery
{
    float3 origin;
    float3 direction;
    float  tMin;
    float  tMax;
};

struct SRayHit
{
    float  t;
    float2 uv;
    uint   triangleId;
};

#if defined( EXTEND_RAY )

cbuffer ExtendRayConstants : register( b0 )
{
    uint g_RayCount;
}

StructuredBuffer<Vertex>      g_Vertices      : register( t0 );
StructuredBuffer<uint>        g_Triangles     : register( t1 );
StructuredBuffer<BVHNode>     g_BVHNodes      : register( t2 );
StructuredBuffer<SRayQuery>   g_RayQueries    : register( t3 );
RWStructuredBuffer<SRayHit>   g_RayHits       : register( u0 );

[numthreads( 256, 1, 1 )]
void main( uint threadId : SV_GroupIndex )
{
    if ( threadId >= g_RayCount )
        return;

    SRayQuery rayQuery = g_RayQueries[ threadId ];
    SRayHit rayHit;
    bool hasHit = BVHIntersectNoInterp( rayQuery.origin
        , rayQuery.direction
        , rayQuery.tMin
        , threadId
        , rayHit.t
        , rayHit.uv.x
        , rayHit.uv.y
        , rayHit.triangleId );
    rayHit.t = hasHit ? rayHit.t : -1.0f;

    g_RayHits[ threadId ] = rayHit;
}

#endif

#if defined( HANDLE_HIT )

cbuffer HandleHitConstants : register( b0 )
{
    uint g_RayCount;
}

StructuredBuffer<SRayHit>   g_RayHits : register( t0 );
RWStructuredBuffer<uint>    g_RayMissIndirectArgs : register( u0 )
RWStructuredBuffer<uint>    g_RayHitMaterialIndirectArgs : register( u1 )
RWStructuredBuffer<uint>    g_RayMissIndices : register( u2 )
RWStructuredBuffer<uint>    g_RayHitMaterialIndices : register( u3 )

[numthreads( 256, 1, 1 )]
void main( uint threadId : SV_GroupIndex )
{
    if ( threadId >= g_RayCount )
        return;

    SRayHit rayHit = g_RayHits[ threadId ];
    bool hasHit = rayHit.t != -1.0f;
    if ( !hasHit )
    {
        uint index = 0;
        InterlockedAdd( g_RayMissIndirectArgs[ 0 ], 1, index );
        g_RayMissIndices[ index ] = threadId;
    }
    else
    {
        uint index = 0;
        InterlockedAdd( g_RayHitMaterialIndirectArgs[ 0 ], 1, index );
        g_RayHitMaterialIndices[ index ] = threadId;
    }
}

#endif

#if defined( FILL_INDIRECT_ARGS )

RWStructuredBuffer<uint>    g_RayMissIndirectArgs : register( u0 )
RWStructuredBuffer<uint>    g_RayHitMaterialIndirectArgs : register( u1 )

[numthreads( 1, 1, 1 )]
void main()
{
    g_RayMissIndirectArgs[ 1 ] = g_RayMissIndirectArgs[ 0 ] / 32;
    if ( ( g_RayMissIndirectArgs[ 0 ] % 32 ) > 0 )
    {
        g_RayMissIndirectArgs[ 1 ] += 1;
    }
    g_RayMissIndirectArgs[ 0 ] = 0;

    g_RayHitMaterialIndirectArgs[ 1 ] = g_RayHitMaterialIndirectArgs[ 0 ] / 32;
    if ( ( g_RayHitMaterialIndirectArgs[ 0 ] % 32 ) > 0 )
    {
        g_RayHitMaterialIndirectArgs[ 1 ] += 1;
    }
    g_RayHitMaterialIndirectArgs[ 0 ] = 0;
}

#endif

#if defined( RAY_MISS )

[numthreads( 32, 1, 1 )]
void main( uint threadId : SV_GroupIndex )
{

}

#endif

#if defined( SHADOW_RAY )

cbuffer ShadowRayConstants : register( b0 )
{
    uint g_RayCount;
}

StructuredBuffer<Vertex>                g_Vertices      : register( t0 );
StructuredBuffer<uint>                  g_Triangles     : register( t1 );
StructuredBuffer<BVHNode>               g_BVHNodes      : register( t2 );
StructuredBuffer<SRayOcclusionQuery>    g_RayQueries    : register( t3 );
RWStructuredBuffer<bool>                g_RayHits       : register( u0 );

[numthreads( 256, 1, 1 )]
void main( uint threadId : SV_GroupIndex )
{
    if ( threadId >= g_RayCount )
        return;

    SRayOcclusionQuery rayQuery = g_RayQueries[ threadId ];
    bool hasHit = BVHIntersect( rayQuery.origin
        , rayQuery.direction
        , rayQuery.tMin
        , rayQuery.tMax
        , threadId );

    g_RayHits[ threadId ] = hasHit;
}

#endif

#if defined( MEGAKERNEL )

#define GROUP_SIZE_X 16
#define GROUP_SIZE_Y 16

[numthreads( GROUP_SIZE_X, GROUP_SIZE_Y, 1 )]
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
    GenerateRay( filmSample, apertureSample, g_FilmSize, g_ApertureRadius, g_FocalDistance, g_FilmDistance, g_BladeCount, g_BladeVertexPos, g_ApertureBaseAngle, g_CameraTransform, intersection.position, wo );

    if ( IntersectScene( intersection.position, wo, threadId, intersection ) )
    {
        uint iBounce = 0;
        while ( 1 )
        {
            wo = -wo;

            SLightSamples lightSamples;
            lightSamples.lightSelectionSample = GetNextSample1D( rng );
            lightSamples.lightSample          = GetNextSample2D( rng );
#if defined( MULTIPLE_IMPORTANCE_SAMPLING )
            lightSamples.bsdfSelectionSample  = GetNextSample1D( rng );
            lightSamples.bsdfSample           = GetNextSample2D( rng );
#endif
            l += pathThroughput * ( UniformSampleOneLight( lightSamples, intersection, wo, threadId ) + intersection.emission );

            if ( iBounce == g_MaxBounceCount )
                break;

            float3 brdf;
            float pdf;
            bool isDeltaBxdf;
            SampleBSDF( wo, GetNextSample2D( rng ), GetNextSample1D( rng ), intersection, wi, brdf, pdf, isDeltaBxdf );

            if ( all( brdf == 0.0f ) || pdf == 0.0f )
                break;

            float NdotL = abs( dot( wi, intersection.normal ) );
            pathThroughput = pathThroughput * brdf * NdotL / pdf;

            if ( !IntersectScene( OffsetRayOrigin( intersection.position, intersection.geometryNormal, wi ), wi, threadId, intersection ) )
            {
                l += pathThroughput * EnvironmentShader( wi );
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
        l = EnvironmentShader( wo );
    }

    AddSampleToFilm( l, pixelSample, pixelPos );
}

#endif

#if defined( OUTPUT_NORMAL ) || defined( OUTPUT_TANGENT ) || defined( OUTPUT_ALBEDO ) || defined( OUTPUT_NEGATIVE_NDOTV ) || defined( OUTPUT_BACKFACE )

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

    if ( IntersectScene( intersection.position, wo, threadId, intersection ) )
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

    AddSampleToFilm( l, pixelSample, pixelPos );
}

#endif