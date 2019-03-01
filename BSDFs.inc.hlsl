
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
    return max(0.0f, wi.z * INV_PI);
}

void SampleLambertBRDF(float4 wo
	, float2 sample
    , float4 albedo
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

void SampleGGXMicrofacetDistribution(float2 sample, float alpha, out float4 m, out float pdf)
{
    m = GGXSampleHemisphere(sample, alpha);
    pdf = EvaluateGGXMicrofacetDistributionPdf(m, alpha);
}

void SampleGGXMicrofacetDistribution(float2 sample, float alpha, out float4 m)
{
    m = GGXSampleHemisphere(sample, alpha);
}

//
// GGX geometric shadowing
//

float EvaluateGGXGeometricShadowingOneDirection(float alpha2, float4 w)
{
    float NdotW = abs(w.z);
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

float EvaluateSchlickFresnel(float WOdotM, float ior, bool isInverted)
{
    float f0 = (1.0f - ior) / (1.0f + ior);
    f0 *= f0;
    
    float x = WOdotM;
    if (isInverted)
    {
        float ratio = 1.0f / ior;
        float d = sqrt(1 - ratio * ratio);
        x = max(0, x / d - d);
    }
    return (1 - f0) * pow(1 - x, 5) + f0;
}

//
// Cook-Torrance microfacet BRDF
//

float4 EvaluateCookTorranceMircofacetBRDF(float4 wi, float4 wo, float4 reflectance, float alpha, float ior, bool isInverted)
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
    return reflectance * EvaluateGGXMicrofacetDistribution(m, alpha) * EvaluateGGXGeometricShadowing(wi, wo, alpha) * EvaluateSchlickFresnel(WOdotM, ior, isInverted) / (4 * WIdotN * WOdotN);
}

float EvaluateCookTorranceMicrofacetBRDFPdf(float4 wi, float4 wo, float alpha)
{
    float WIdotN = wi.z;
    float WOdotN = wo.z;
    if (WIdotN * WOdotN > 0.0f)
    {
        float4 m = normalize(wi + wo);
        float pdf = EvaluateGGXMicrofacetDistributionPdf(m, alpha);
        return pdf / (4 * dot(wi, m));
    }
    else
    {
        return 0.0f;
    }
}

void SampleCookTorranceMicrofacetBRDF(float4 wo, float2 sample, float4 reflectance, float alpha, float ior, bool isInverted, out float4 wi, out float4 value, out float pdf)
{
    float4 m;
    SampleGGXMicrofacetDistribution(sample, alpha, m);
    wi = -reflect(wo, m);
    if (wi.z * wo.z <= 0.0f)
    {
        value = 0.0f;
        pdf = 0.0f;
    }
    else
    {
        value = EvaluateCookTorranceMircofacetBRDF(wi, wo, reflectance, alpha, ior, isInverted);
        pdf = EvaluateCookTorranceMicrofacetBRDFPdf(wi, wo, alpha);
    }
}

//
// Cook-Torrance BTDF
//

float EvaluateCookTorranceMicrofacetBTDFPdf(float4 wi, float4 wo, float4 m, float alpha, float no, float ni)
{
    float WIdotM = abs(dot(wi, m));
    float WOdotM = abs(dot(wo, m));
    float pdf = EvaluateGGXMicrofacetDistributionPdf(m, alpha);
    float term = no * WOdotM + ni * WIdotM;
    return pdf * WOdotM * ni * ni / (term * term);
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

    float4 m;
    SampleGGXMicrofacetDistribution(GetNextSample2(), intersection.alpha, m);
    float WOdotM = max(dot(wo, m), 0.0f);
    bool isInverted = wo.z < 0.0f;
    float fresnel = EvaluateSchlickFresnel(WOdotM, intersection.ior, isInverted);
    float4 lambertBRDF = EvaluateLambertBRDF(wi, intersection.albedo) * (1.0f - fresnel);
    float4 cooktorranceBRDF = EvaluateCookTorranceMircofacetBRDF(wi, wo, intersection.specular, intersection.alpha, intersection.ior, isInverted);
    return lambertBRDF + cooktorranceBRDF;
}

static const int BRDFComponentLambert = 0;
static const int BRDFComponentReflection = 1;
static const int BRDFComponentRefraction = 2;

void SampleBSDF(float4 wo
    , float2 BRDFSample
    , float BRDFSelectionSample
    , Intersection intersection
    , out float4 wi
    , out float4 value
    , out float pdf)
{
    float4 biNormal = float4(cross(intersection.tangent.xyz, intersection.normal.xyz), 0.0f);
    float4x4 tbn2world = float4x4(intersection.tangent, biNormal, intersection.normal, float4(0.0f, 0.0f, 0.0f, 1.0f));
    float4x4 world2tbn = transpose(tbn2world);

    wo = mul(wo, world2tbn);

    bool isInverted = false;
    float no, ni;
    if (wo.z < 0.0f)
    {
        wo.z = -wo.z;
        no = intersection.ior;
        ni = 1.0f;
        isInverted = true;
    }
    else
    {
        no = 1.0f;
        ni = intersection.ior;
    }

    int BRDFComponent = 0;
    float BRDFComponentPdf = 0.0f;

    float4 m;
    SampleGGXMicrofacetDistribution(BRDFSample, intersection.alpha, m);

    float WOdotM = dot(wo, m);

    float fresnel = EvaluateSchlickFresnel(WOdotM, intersection.ior, isInverted);
    wi = -reflect(wo, m);

    if (WOdotM <= 0.0f)
    {
        fresnel = 0.0f;
        BRDFComponent = BRDFComponentLambert;
        BRDFComponentPdf = 1.0f;
    }
    else if (wi.z < 0.0f || BRDFSelectionSample >= fresnel)
    {
        if (GetNextSample() < intersection.albedo.a)
        {
            BRDFComponent = BRDFComponentLambert;
            BRDFComponentPdf = intersection.albedo.a;
        }
        else
        {
            BRDFComponent = BRDFComponentRefraction;
            BRDFComponentPdf = 1.0f - intersection.albedo.a;
        }

        if (wi.z < 0.0f)
        {
            fresnel = 0.0f;
        }

        BRDFComponentPdf *= 1.0f - fresnel;
    }
    else
    {
        BRDFComponent = BRDFComponentReflection;
        BRDFComponentPdf = fresnel;
    }

    if (BRDFComponent == BRDFComponentLambert)
    {
        if (!isInverted)
        {
            SampleLambertBRDF(wo, GetNextSample2(), intersection.albedo, wi, value, pdf);
            value *= intersection.albedo.a * (1.0f - fresnel);
        }
        else
        {
            value = 0.0f;
        }
    }
    else if (BRDFComponent == BRDFComponentReflection)
    {
        value = EvaluateCookTorranceMircofacetBRDF(wi, wo, intersection.specular, intersection.alpha, intersection.ior, isInverted);
        pdf = EvaluateCookTorranceMicrofacetBRDFPdf(wi, wo, intersection.alpha);
    }
    else if (BRDFComponent == BRDFComponentRefraction)
    {
        wi = refract(-wo, m, no / ni);

        if (all(wi.xyz == 0.0f) || wi.z == 0.0f)
        {
            value = 0.0f;
        }
        else
        {
            float WIdotM = dot(wi, m);
            value = ni * ni * abs(WIdotM) * abs(WOdotM) * (1.0f - fresnel) * EvaluateGGXGeometricShadowing(wi, wo, intersection.alpha) * EvaluateGGXMicrofacetDistribution(m, intersection.alpha);
            float term = ni * abs(WIdotM) + no * abs(WOdotM);
            value /= abs(wi.z) * abs(wo.z) * term * term;
            value *= 1.0f - intersection.albedo.a;
            pdf = EvaluateCookTorranceMicrofacetBTDFPdf(wi, wo, m, intersection.alpha, no, ni);
        }
    }

    pdf *= BRDFComponentPdf;

    if (isInverted)
        wi.z = -wi.z;

    wi = mul(wi, tbn2world);
}
