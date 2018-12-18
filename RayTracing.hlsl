
struct Sphere
{
    float3 position;
    float radius;
};


struct RayTracingConstants
{
    uint maxBounceCount;
	float2 filmSize;
	float filmDistance;
	float4x4 cameraTransform;
    float4 background;
};


struct Ray
{
    float4 origin;
    float4 direction;
};


StructuredBuffer<Sphere> g_Spheres;
StructuredBuffer<RayTracingConstants> g_Constants;
RWTexture2D<float4> g_FilmTexture;

float2 GetNextSample2()
{
    return (float2)0.0f;
}

void GenerateRay(float2 sample
	, float2 filmSize
	, float filmDistance
	, float4x4 cameraTransform
	, out float4 origin
	, out float4 direction)
{
}

void SampleLambertBRDF(float4 wo
	, float2 sample
    , float4 albedo
	, out float4 wi
	, out float4 value
	, out float4 pdf)
{

}

bool IntersectScene(float4 origin
	, float4 direction
    , float4 epsilon
	, out float4 position
	, out float4 normal
	, out float4 albedo)
{

}

void AddSampleToFilm(float4 l
    , float2 sample
    , uint2 pixelPos)
{

}

[numthreads(1, 1, 1)]
void main(uint threadId : SV_GroupIndex, uint2 pixelPos : SV_DispatchThreadID)
{
    float4 pathThroughput = (float4) 1.0f;
    float4 normal, position, l, wi, wo, albedo;

    float2 pixelSample = GetNextSample2();
    GenerateRay(pixelSample, g_Constants[0].filmSize, g_Constants[0].filmDistance, g_Constants[0].cameraTransform, position, wo);

    uint iBounce = 0;
    while (1)
    {
        if (iBounce == g_Constants[0].maxBounceCount)
            break;

		float4 brdf, pdf;
        SampleLambertBRDF(wo, GetNextSample2(), albedo, wi, brdf, pdf);

        float NdotL = dot(wi, normal);
		pathThroughput = pathThroughput * brdf * NdotL / pdf;

        if (!IntersectScene(position, wi, 0.00001f, position, normal, albedo))
        {
            l += pathThroughput * g_Constants[0].background;
            break;
        }

        ++iBounce;
    }

    AddSampleToFilm(l, pixelSample, pixelPos);
}