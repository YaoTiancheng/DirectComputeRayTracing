
struct Sphere
{
    float4  position;
    float   radius;
    float4  albedo;
    float   metallic;
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


struct Intersection
{
    float4  albedo;
    float4  specular;
    float4  emission;
    float   alpha;
    float4  position;
    float4  normal;
    float4  tangent;
    float   rayEpsilon;
    float   f0;
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
	, out Intersection intersection)
{
    float tMin = 1.0f / 0.0f;

    for (int i = 0; i < g_Constants[0].sphereCount; ++i)
    {
        float t;
        Intersection testIntersection;
        if (RaySphereIntersect(origin + direction * epsilon, direction, g_Spheres[i], t, testIntersection))
        {
            if (t < tMin)
            {
                tMin = t;
                intersection = testIntersection;
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

float4 EstimateDirect(PointLight pointLight, Intersection intersection, float epsilon, float4 wo)
{
    float4 wi = pointLight.position - intersection.position;
    float len = length(wi);
    wi /= len;

    if (!IsOcculuded(intersection.position, wi, epsilon, len))
    {
        float4 l = pointLight.color / (len * len);
        float4 brdf = EvaluateBSDF(wi, wo, intersection);
        float NdotL = max(0, dot(intersection.normal, wi));
        return l * brdf * NdotL;
    }

    return 0.0f;
}

float4 UniformSampleOneLight(float sample, Intersection intersection, float4 wo, float epsilon)
{
    if (g_Constants[0].pointLightCount == 0)
    {
        return 0.0f;
    }
    else
    {
        uint lightIndex = floor(GetNextSample() * g_Constants[0].pointLightCount);
        return EstimateDirect(g_PointLights[lightIndex], intersection, epsilon, wo) * g_Constants[0].pointLightCount; 
    }
}

[numthreads(32, 32, 1)]
void main(uint threadId : SV_GroupIndex, uint2 pixelPos : SV_DispatchThreadID)
{
    if (any(pixelPos > g_Constants[0].resolution))
        return;

    float4 pathThroughput = (float4) 1.0f;
    float4 l = (float4) 0.0f;
    float4 wi, wo;
    Intersection intersection;

    float2 pixelSample = GetNextSample2();
    float2 filmSample = (pixelSample + pixelPos) / g_Constants[0].resolution;
    GenerateRay(filmSample, g_Constants[0].filmSize, g_Constants[0].filmDistance, g_Constants[0].cameraTransform, intersection.position, wo);

    if (IntersectScene(intersection.position, wo, 0.0f, intersection))
    {
        uint iBounce = 0;
        while (1)
        {
            wo = -wo;

            float lightSelectionSample = GetNextSample();
            l += pathThroughput * (UniformSampleOneLight(lightSelectionSample, intersection, wo, intersection.rayEpsilon) + intersection.emission);

            if (iBounce == g_Constants[0].maxBounceCount)
                break;

            float4 brdf;
            float pdf;
            SampleBSDF(wo, GetNextSample2(), GetNextSample(), intersection, wi, brdf, pdf);

            if (all(brdf.xyz == 0.0f) || pdf == 0.0f)
                break;

            float NdotL = dot(wi, intersection.normal);
            pathThroughput = pathThroughput * brdf * NdotL / pdf;

            if (!IntersectScene(intersection.position, wi, intersection.rayEpsilon, intersection))
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