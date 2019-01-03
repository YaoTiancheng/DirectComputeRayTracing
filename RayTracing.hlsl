
struct Sphere
{
    float4  position;
    float   radius;
    float4  albedo;
    float4  emission;
};


struct PointLight
{
    float4 position;
    float4 color;
};


struct RayTracingConstants
{
    uint                maxBounceCount;
    uint                sphereCount;
    uint                pointLightCount;
    uint                samplesCount;
    float2              resolution;
    float2              filmSize;
    float               filmDistance;
    row_major float4x4  cameraTransform;
    float4              background;
};


StructuredBuffer<Sphere>                g_Spheres       : register(t0);
StructuredBuffer<PointLight>            g_PointLights   : register(t1);
StructuredBuffer<RayTracingConstants>   g_Constants     : register(t2);
RWTexture2D<float4>                     g_FilmTexture;

static const float PI       = 3.14159265359;
static const float INV_PI   = 1 / PI;

#include "Samples.inc.hlsl"
#include "MonteCarlo.inc.hlsl"
#include "BSDFs.inc.hlsl"
#include "Primitives.inc.hlsl"


void GenerateRay(float2 sample
	, float2 filmSize
	, float filmDistance
	, float4x4 cameraTransform
	, out float4 origin
	, out float4 direction)
{
    float2 filmSample = float2(sample.x - 0.5f, -sample.y + 0.5f) * filmSize;
    origin = float4(0.0f, 0.0f, filmDistance, 1.0f);
    direction = normalize(origin - float4(filmSample, 0.0f, 1.0f));
    direction.xy = -direction.xy;

    origin = mul(origin, cameraTransform);
    direction = mul(direction, cameraTransform);
}

bool IntersectScene(float4 origin
	, float4 direction
    , float4 epsilon
	, out float4 position
	, out float4 normal
    , out float4 tangent
    , out float4 albedo
    , out float4 emission)
{
    float tMin = 1.0f / 0.0f;

    for (int i = 0; i < g_Constants[0].sphereCount; ++i)
    {
        float t;
        float4 testPosition, testNormal, testTangent, testAlbedo, testEmission;
        if (RaySphereIntersect(origin + direction * epsilon, direction, g_Spheres[i], t, testPosition, testNormal, testTangent, testAlbedo, testEmission))
        {
            if (t < tMin)
            {
                tMin = t;
                position = testPosition;
                normal = testNormal;
                tangent = testTangent;
                albedo = testAlbedo;
                emission = testEmission;
            }
        }
    }

    return !isinf(tMin);
}

bool IsOcculuded(float4 origin
    , float4 direction
    , float4 epsilon
    , float distance)
{
    for (int i = 0; i < g_Constants[0].sphereCount; ++i)
    {
        float t;
        if (RaySphereIntersect(origin + direction * epsilon, direction, g_Spheres[i], t))
        {
            if (t < distance)
                return true;
        }
    }
    return false;
}

void AddSampleToFilm(float4 l
    , float2 sample
    , uint2 pixelPos)
{
    float4 c = g_FilmTexture[pixelPos];
    c += float4(l.xyz, 1.0f);
    g_FilmTexture[pixelPos] = c;
}

float4 EstimateDirect(PointLight pointLight, float4 position, float4 normal, float epsilon, float4 wo, float4 albedo)
{
    float4 wi = pointLight.position - position;
    float len = length(wi);
    wi /= len;

    if (!IsOcculuded(position, wi, epsilon, len))
    {
        float4 l = pointLight.color / (len * len);
        float4 brdf = EvaluateLambertBRDF(wo, wi, albedo);
        float NdotL = max(0, dot(normal, wi));
        return l * brdf * NdotL;
    }

    return 0.0f;
}

float4 UniformSampleOneLight(float sample, float4 position, float4 normal, float4 wo, float4 albedo, float epsilon)
{
    uint lightIndex = floor(GetNextSample() * g_Constants[0].pointLightCount);
    return EstimateDirect(g_PointLights[lightIndex], position, normal, epsilon, wo, albedo) * g_Constants[0].pointLightCount; 
}

[numthreads(32, 32, 1)]
void main(uint threadId : SV_GroupIndex, uint2 pixelPos : SV_DispatchThreadID)
{
    if (any(pixelPos > g_Constants[0].resolution))
        return;

    float4 pathThroughput = (float4) 1.0f;
    float4 l = (float4) 0.0f;
    float4 normal, tangent, position, wi, wo, albedo, emission;

    float2 pixelSample = GetNextSample2();
    float2 filmSample = (pixelSample + pixelPos) / g_Constants[0].resolution;
    GenerateRay(filmSample, g_Constants[0].filmSize, g_Constants[0].filmDistance, g_Constants[0].cameraTransform, position, wo);

    if (IntersectScene(position, wo, 0.00001f, position, normal, tangent, albedo, emission))
    {
        uint iBounce = 0;
        while (1)
        {
            float lightSelectionSample = GetNextSample();
            l += pathThroughput * (UniformSampleOneLight(lightSelectionSample, position, normal, -wo, albedo, 0.000001f) + emission);

            if (iBounce == g_Constants[0].maxBounceCount)
                break;

            float4 brdf, pdf;
            SampleLambertBRDF(-wo, GetNextSample2(), albedo, normal, tangent, wi, brdf, pdf);

            // Sometimes BRDF value at wi is zero.
            if (all(brdf == 0.0f))
                break;

            float NdotL = dot(wi, normal);
            pathThroughput = pathThroughput * brdf * NdotL / pdf;

            if (!IntersectScene(position, wi, 0.00001f, position, normal, tangent, albedo, emission))
            {
                l += pathThroughput * g_Constants[0].background;
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
        l = g_Constants[0].background;
    }

    AddSampleToFilm(l, pixelSample, pixelPos);
}