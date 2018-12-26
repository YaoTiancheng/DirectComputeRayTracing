
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
StructuredBuffer<float>                 g_Samples       : register(t3);
RWTexture2D<float4>                     g_FilmTexture;

#define PI 3.1415

groupshared uint gs_NextSampleIndex = 0;

float GetNextSample()
{
    uint sampleIndex;
    InterlockedAdd(gs_NextSampleIndex, 1, sampleIndex);
    sampleIndex = sampleIndex % g_Constants[0].samplesCount;
    return g_Samples[sampleIndex];
}

float2 GetNextSample2()
{
    return float2(GetNextSample(), GetNextSample());
}

float2 ConcentricSampleDisk(float2 sample)
{
    float r, theta;
    float2 s = 2 * sample - 1;

    if (s.x == 0.0f && s.y == 0.0f)
    {
        return (float2) 0.0f;
    }
    if (s.x >= -s.y)
    {
        if (s.x > s.y)
        {
            r = s.x;
            if (s.y > 0.0f)
                theta = s.y / r;
            else
                theta = 8.0f + s.y / r;
        }
        else
        {
            r = s.y;
            theta = 2.0f - s.x / r;
        }
    }
    else
    {
        if (s.x <= s.y)
        {
            r = -s.x;
            theta = 4.0f - s.y / r;
        }
        else
        {
            r = -s.y;
            theta = 6.0f + s.x / r;
        }
    }

    theta *= PI / 4.0f;
    return r * float2(cos(theta), sin(theta));
}

float4 ConsineSampleHemisphere(float2 sample)
{
    sample = ConcentricSampleDisk(sample);
    return float4(sample.xy, sqrt(max(0.0f, 1.0f - dot(sample.xy, sample.xy))), 0.0f);
}

bool RaySphereIntersect(float4 origin
    , float4 direction
    , Sphere sphere
    , out float t)
{
    float radius2 = sphere.radius * sphere.radius;
    float t0, t1;
    float4 l = sphere.position - origin;
    float tca = dot(l, direction);
    if (tca < 0.0f)
        return false;
    float d2 = dot(l, l) - tca * tca;
    if (d2 > radius2)
        return false;
    float thc = sqrt(radius2 - d2);
    t0 = tca - thc;
    t1 = tca + thc;

    if (t0 > t1)
    {
        float temp = t0;
        t0 = t1;
        t1 = temp;
    }
    if (t0 < 0)
    {
        t0 = t1;
        if (t0 < 0)
            return false;
    }
    t = t0;
    return true;
}

bool RaySphereIntersect(float4 origin
    , float4 direction
    , Sphere sphere
    , out float t
    , out float4 position
    , out float4 normal
    , out float4 tangent
    , out float4 albedo
    , out float4 emission)
{
    bool intersect = false;
    if (intersect = RaySphereIntersect(origin, direction, sphere, t))
    {
        position = origin + t * direction;
        normal = normalize(position - sphere.position);
        tangent = normalize(float4(cross(float3(0.0f, 1.0f, 0.0f), normal.xyz), 0.0f));
        if (isinf(tangent.x))
            tangent = float4(1.0f, 0.0f, 0.0f, 0.0f);
        albedo = sphere.albedo;
        emission = sphere.emission;
    }
    return intersect;
}

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

void SampleLambertBRDF(float4 wo
	, float2 sample
    , float4 albedo
    , float4 normal
    , float4 tangent
    , out float4 wi
    , out float4 value
    , out float4 pdf)
{
    float4 biNormal = float4(cross(tangent.xyz, normal.xyz), 0.0f);
    float4x4 tbn2world = float4x4(tangent, biNormal, normal, float4(0.0f, 0.0f, 0.0f, 1.0f));
    float4x4 world2tbn = transpose(tbn2world);

    wo = mul(wo, world2tbn);

    wi = ConsineSampleHemisphere(sample);
    value = wi.z > 0.0f ? albedo / PI : 0.0f;
    pdf = dot(float4(0.0f, 0.0f, 1.0f, 0.0f), wi) / PI;

    wi = mul(wi, tbn2world);
}

float4 EvaluateLambertBRDF(float4 wo, float4 wi, float4 albedo)
{
    return albedo / PI;
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