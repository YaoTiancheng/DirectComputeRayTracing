
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
#include "HitShader.inc.hlsl"
#include "BVHAccel.inc.hlsl"
#include "Intrinsics.inc.hlsl"

SamplerState UVClampSampler : register( s0 );
SamplerState UVWrapSampler : register( s1 );

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
    float3 p_i = asfloat( asint( p ) + select( p < 0, -of_i, of_i ) );
    return select( abs( p ) < s_Origin, p + s_FloatScale * n, p_i );
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
    , Texture2D<float4> textures[]
    , SamplerState samplerState
    , inout Intersection intersection )
{
    intersection.lightIndex = instanceLightIndices[ hitInfo.instanceIndex ];
    intersection.triangleIndex = hitInfo.triangleId;

    Vertex v0 = vertices[ triangles[ hitInfo.triangleId * 3 ] ];
    Vertex v1 = vertices[ triangles[ hitInfo.triangleId * 3 + 1 ] ];
    Vertex v2 = vertices[ triangles[ hitInfo.triangleId * 3 + 2 ] ];
    HitShader( origin, direction, v0, v1, v2, hitInfo.t, hitInfo.u, hitInfo.v, hitInfo.triangleId, hitInfo.backface, materialIds, materials, textures, samplerState, intersection );
    // Transform the position & vectors from local space to world space. Assuming the transform only contains uniform scaling otherwise the transformed vectors are wrong.
    intersection.position = mul( float4( intersection.position, 1.f ), instances[ hitInfo.instanceIndex ] );
    intersection.normal = normalize( mul( float4( intersection.normal, 0.f ), instances[ hitInfo.instanceIndex ] ) );
    intersection.geometryNormal = normalize( mul( float4( intersection.geometryNormal, 0.f ), instances[ hitInfo.instanceIndex ] ) );
    intersection.tangent = normalize( mul( float4( intersection.tangent, 0.f ), instances[ hitInfo.instanceIndex ] ) );
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

SLightSampleResult SampleLightDirect( float3 p
    , StructuredBuffer<SLight> lights
    , uint lightCount
    , StructuredBuffer<Vertex> vertices
    , StructuredBuffer<uint> triangles
    , StructuredBuffer<float4x3> instanceTransforms
    , TextureCube<float3> envTexture
    , SamplerState envTextureSampler
    , inout Xoshiro128StarStar rng )
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
    else if ( light.flags & LIGHT_FLAGS_DIRECTIONAL_LIGHT )
    {
        DirectionalLight_Sample( light, result.radiance, result.wi, result.distance, result.pdf );
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
    }
    else if ( light.flags & LIGHT_FLAGS_ENVIRONMENT_LIGHT )
    {
        float2 samples = GetNextSample2D( rng );
        EnvironmentLight_Sample( light, samples, envTexture, envTextureSampler, result.radiance, result.wi, result.distance, result.pdf );
    }

    result.pdf /= lightCount;

    if ( result.distance != FLT_INF )
    {
        result.distance *= 1 - SHADOW_EPSILON;
    }
    return result;
}

void EvaluateLightDirect( uint lightIndex
    , uint triangleIndex
    , float3 normal
    , float3 wi
    , float distance
    , StructuredBuffer<SLight> lights
    , uint lightCount
    , StructuredBuffer<Vertex> vertices
    , StructuredBuffer<uint> triangles
    , StructuredBuffer<float4x3> instanceTransforms
    , TextureCube<float3> envTexture
    , SamplerState envTextureSampler
    , out float3 radiance
    , out float pdf )
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
    else if ( light.flags & LIGHT_FLAGS_ENVIRONMENT_LIGHT )
    {
        EnvironmentLight_EvaluateWithPDF( light, wi, envTexture, envTextureSampler, radiance, pdf );
    }
    
    pdf /= lightCount;
}