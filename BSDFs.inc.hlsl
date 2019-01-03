
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
    value = wi.z > 0.0f ? albedo * INV_PI : 0.0f;
    pdf = dot(float4(0.0f, 0.0f, 1.0f, 0.0f), wi) * INV_PI;

    wi = mul(wi, tbn2world);
}

float4 EvaluateLambertBRDF(float4 wo, float4 wi, float4 albedo)
{
    return albedo * INV_PI;
}

float4 SampleGGXHemisphere(float2 sample, float alpha)
{
}