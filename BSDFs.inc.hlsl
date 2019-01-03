
float4 ConsineSampleHemisphere(float2 sample)
{
    sample = ConcentricSampleDisk(sample);
    return float4(sample.xy, sqrt(max(0.0f, 1.0f - dot(sample.xy, sample.xy))), 0.0f);
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
    value = wi.z > 0.0f ? albedo * INV_PI : 0.0f;
    pdf = dot(float4(0.0f, 0.0f, 1.0f, 0.0f), wi) * INV_PI;

    wi = mul(wi, tbn2world);
}

float4 EvaluateLambertBRDF(float4 wo, float4 wi, float4 albedo)
{
    return albedo * INV_PI;
}

float4 GGXSampleHemisphere(float2 sample, float alpha)
{
	float theta = atan(alpha * sqrt(sample.x / (1.0f - sample.x)));
	float phi = 2 * PI * sample.y;

    float4 v = float4(cos(phi), sin(phi), 0.0f, 0.0f);
	v *= sin(theta);
	v.z = cos(theta);
	return v;
}

float EvaluateGGXMicrofacetDistribution(float4 m, float alpha)
{
	float alpha2 = alpha * alpha;
	float NdotM = m.z;
	float NdotM2 = NdotM * NdotM;
    float factor = NdotM2 * (alpha2 - 1) + 1;
	float denominator = factor * factor * PI;
	return alpha2 / denominator;
}