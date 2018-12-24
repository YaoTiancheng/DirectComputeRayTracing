
struct Sphere
{
    float4  position;
    float   radius;
    float4  albedo;
};


struct RayTracingConstants
{
    uint                maxBounceCount;
    uint                sphereCount;
    uint                samplesCount;
    float2              resolution;
    float2              filmSize;
    float               filmDistance;
    row_major float4x4  cameraTransform;
    float4              background;
};


StructuredBuffer<Sphere>                g_Spheres   : register(t0);
StructuredBuffer<RayTracingConstants>   g_Constants : register(t1);
StructuredBuffer<float>                 g_Samples   : register(t2);
RWTexture2D<float4>                     g_FilmTexture;

#define PI 3.1415

groupshared uint gs_NextSampleIndex = 0;

float2 GetNextSample2()
{
    float2 sample;
    uint sampleIndex;

    InterlockedAdd(gs_NextSampleIndex, 1, sampleIndex);
    sampleIndex = sampleIndex % g_Constants[0].samplesCount;
    sample.x = g_Samples[sampleIndex];

    InterlockedAdd(gs_NextSampleIndex, 1, sampleIndex);
    sampleIndex = sampleIndex % g_Constants[0].samplesCount;
    sample.y = g_Samples[sampleIndex];

    return sample;
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
    return float4(sample.xy, max(0.0f, 1.0f - dot(sample.xy, sample.xy)), 0.0f);
}

bool RaySphereIntersect(float4 origin
    , float4 direction
    , Sphere sphere
    , out float t
    , out float4 position
    , out float4 normal
    , out float4 tangent
    , out float4 albedo)
{
    t = 0.0f;
    position = (float4) 0.0f;
    normal = (float4) 0.0f;
    tangent = (float4) 0.0f;
    albedo = (float4) 0.0f;

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
    position = origin + t * direction;
    normal = normalize(position - sphere.position);
    tangent = float4(cross(float3(0.0f, 1.0f, 0.0f), normal.xyz), 0.0f);
    if (length(tangent) < 0.000001f) 
        tangent = float4(1.0f, 0.0f, 0.0f, 0.0f);
    else 
        tangent = normalize(tangent);
    albedo = sphere.albedo;

    return true;
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

bool IntersectScene(float4 origin
	, float4 direction
    , float4 epsilon
	, out float4 position
	, out float4 normal
    , out float4 tangent
    , out float4 albedo)
{
    float tMin = 1.0f / 0.0f;

    for (int i = 0; i < g_Constants[0].sphereCount; ++i)
    {
        float t;
        float4 testPosition, testNormal, testTangent, testAlbedo;
        if (RaySphereIntersect(origin + direction * epsilon, direction, g_Spheres[i], t, testPosition, testNormal, testTangent, testAlbedo))
        {
            if (t < tMin)
            {
                tMin = t;
                position = testPosition;
                normal = testNormal;
                tangent = testTangent;
                albedo = testAlbedo;
            }
        }
    }

    return !isinf(tMin);
}

void AddSampleToFilm(float4 l
    , float2 sample
    , uint2 pixelPos)
{
    float4 c = g_FilmTexture[pixelPos];
    c += float4(l.xyz, 1.0f);
    g_FilmTexture[pixelPos] = c;
}

[numthreads(32, 32, 1)]
void main(uint threadId : SV_GroupIndex, uint2 pixelPos : SV_DispatchThreadID)
{
    if (any(pixelPos > g_Constants[0].resolution))
        return;

    float4 pathThroughput = (float4) 1.0f;
    float4 l = (float4) 0.0f;
    float4 normal, tangent, position, wi, wo, albedo;

    float2 pixelSample = GetNextSample2();
    float2 filmSample = (pixelSample + pixelPos) / g_Constants[0].resolution;
    GenerateRay(filmSample, g_Constants[0].filmSize, g_Constants[0].filmDistance, g_Constants[0].cameraTransform, position, wo);

    if (IntersectScene(position, wo, 0.00001f, position, normal, tangent, albedo))
    {
        uint iBounce = 0;
        while (1)
        {
            if (iBounce == g_Constants[0].maxBounceCount)
                break;

            float4 brdf, pdf;
            SampleLambertBRDF(-wo, GetNextSample2(), albedo, normal, tangent, wi, brdf, pdf);

            // Sometimes BRDF value at wi is zero.
            if (all(brdf == 0.0f))
                break;

            float NdotL = dot(wi, normal);
            pathThroughput = pathThroughput * brdf * NdotL / pdf;

            if (!IntersectScene(position, wi, 0.00001f, position, normal, tangent, albedo))
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