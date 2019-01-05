
//
// Lambert BRDF
//

float4 ConsineSampleHemisphere(float2 sample)
{
    sample = ConcentricSampleDisk(sample);
    return float4(sample.xy, sqrt(max(0.0f, 1.0f - dot(sample.xy, sample.xy))), 0.0f);
}

float4 EvaluateLambertBRDF(float4 wi, float4 albedo)
{
    return wi.z > 0.0f ? albedo * INV_PI : 0.0f;
}

float EvaluateLambertBRDFPdf(float4 wi)
{
    return wi.z * INV_PI;
}

void SampleLambertBRDF(float4 wo
	, float2 sample
    , float4 albedo
    , float4 normal
    , float4 tangent
    , out float4 wi
    , out float4 value
    , out float pdf)
{
    wi = ConsineSampleHemisphere(sample);
    value = EvaluateLambertBRDF(wi, albedo);
    pdf = EvaluateLambertBRDFPdf(wi);
}

//
// GGX microfacet distribution
//

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

float EvaluateGGXMicrofacetDistributionPdf(float4 m, float alpha)
{
    return EvaluateGGXMicrofacetDistribution(m, alpha) * m.z;
}

void SampleGGXMicrofacetDistribution(float4 wo
    , float2 sample
    , float alpha
    , out float4 m
    , out float pdf)
{
    m = GGXSampleHemisphere(sample, alpha);
    pdf = EvaluateGGXMicrofacetDistributionPdf(m, alpha);
}

void SampleGGXMicrofacetDistribution(float4 wo
    , float2 sample
    , float alpha
    , out float4 m)
{
    m = GGXSampleHemisphere(sample, alpha);
}

//
// GGX geometric shadowing
//

float EvaluateGGXGeometricShadowingOneDirection(float alpha2, float4 w)
{
    float NdotW = w.z;
    float denominator = sqrt(alpha2 + (1 - alpha2) * NdotW * NdotW) + NdotW;
    return 2 * NdotW / denominator;
}

float EvaluateGGXGeometricShadowing(float4 wi, float4 wo, float alpha)
{
    float alpha2 = alpha * alpha;
    return EvaluateGGXGeometricShadowingOneDirection(alpha2, wi) * EvaluateGGXGeometricShadowingOneDirection(alpha2, wo);
}

//
// SchlickFresnel
//

float EvaluateSchlickFresnel(float WOdotM, float f0)
{
    return (1 - f0) * pow(1 - WOdotM, 5) + f0;
}

//
// Cook-Torrance microfacet BRDF
//

float4 EvaluateCookTorranceMircofacetBRDF(float4 wi, float4 wo, float4 reflectance, float alpha, float f0)
{
    float WIdotN = wi.z;
    float WOdotN = wo.z;
    if (WIdotN <= 0.0f || WOdotN <= 0.0f)
        return 0.0f;
    float4 m = wi + wo;
    if (all(m == 0.0f))
        return 0.0f;
    m = normalize(m);
    float WOdotM = dot(wo, m);
    return reflectance * EvaluateGGXMicrofacetDistribution(m, alpha) * EvaluateGGXGeometricShadowing(wi, wo, alpha) * EvaluateSchlickFresnel(WOdotM, f0) / (4 * WIdotN * WOdotN);
}

float EvaluateCookTorranceMicrofacetBRDFPdf(float4 wi, float4 wo, float alpha)
{
    float WIdotN = wi.z;
    float WOdotN = wo.z;
    if (WIdotN * WOdotN > 0.0f)
    {
        float4 m = normalize(wi + wo);
        float pdf = EvaluateGGXMicrofacetDistributionPdf(m, alpha);
        return pdf / (4 * dot(wo, m));
    }
    else
    {
        return 0.0f;
    }
}

void SampleCookTorranceMicrofacetBRDF(float4 wo, float2 sample, float4 reflectance, float alpha, float f0, out float4 wi, out float4 value, out float pdf)
{
    float4 m;
    SampleGGXMicrofacetDistribution(wo, sample, alpha, m);
    wi = -reflect(wo, m);
    if (wi.z * wo.z <= 0.0f)
    {
        value = 0.0f;
        pdf = 0.0f;
    }
    else
    {
        value = EvaluateCookTorranceMircofacetBRDF(wi, wo, reflectance, alpha, f0);
        pdf = EvaluateCookTorranceMicrofacetBRDFPdf(wi, wo, alpha);
    }
}

//
// BSDF
// 

float4 EvaluateBSDF(float4 wi, float4 wo, Intersection intersection)
{
    float4 biNormal = float4(cross(intersection.tangent.xyz, intersection.normal.xyz), 0.0f);
    float4x4 tbn2world = float4x4(intersection.tangent, biNormal, intersection.normal, float4(0.0f, 0.0f, 0.0f, 1.0f));
    float4x4 world2tbn = transpose(tbn2world);

    wo = mul(wo, world2tbn);
    wi = mul(wi, world2tbn);

    //return EvaluateLambertBRDF(wi, intersection.albedo);
    return EvaluateCookTorranceMircofacetBRDF(wi, wo, intersection.albedo, intersection.alpha, 0.2f);
}

void SampleBSDF(float4 wo
    , float2 sample
    , Intersection intersection
    , out float4 wi
    , out float4 value
    , out float pdf)
{
    float4 biNormal = float4(cross(intersection.tangent.xyz, intersection.normal.xyz), 0.0f);
    float4x4 tbn2world = float4x4(intersection.tangent, biNormal, intersection.normal, float4(0.0f, 0.0f, 0.0f, 1.0f));
    float4x4 world2tbn = transpose(tbn2world);

    wo = mul(wo, world2tbn);

    //SampleLambertBRDF(wo, sample, intersection.albedo, intersection.normal, intersection.tangent, wi, value, pdf);
    SampleCookTorranceMicrofacetBRDF(wo, sample, intersection.albedo, intersection.alpha, 1.0f, wi, value, pdf);

    wi = mul(wi, tbn2world);
}
