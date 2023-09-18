#ifndef _BSDF_H_
#define _BSDF_H_

#include "LambertBRDF.inc.hlsl"
#include "SpecularBxDF.inc.hlsl"
#include "CookTorranceBSDF.inc.hlsl"
#include "Intersection.inc.hlsl"
#include "LightingContext.inc.hlsl"

#define ALPHA_THRESHOLD 0.00052441f

float SpecularWeight( float cosTheta, float alpha, float ior )
{
    return SampleCookTorranceMicrofacetBRDFEnergyFresnelDielectricTexture( cosTheta, alpha, ior );
}

//
// BSDF
// 

float3 EvaluateBSDF( float3 wi, float3 wo, Intersection intersection )
{
    float3 biNormal = cross( intersection.normal, intersection.tangent );
    float3x3 tbn2world = float3x3( intersection.tangent, biNormal, intersection.normal );
    float3x3 world2tbn = transpose( tbn2world );

    wo = mul( wo, world2tbn );
    wi = mul( wi, world2tbn );

    bool isInverted = wo.z < 0.0f;
    if ( isInverted )
    {
        wo.z = -wo.z;
        wi.z = -wi.z;
    }

    float cosThetaO = wo.z;
    LightingContext lightingContext = LightingContextInit( wo, wi, isInverted );

    bool perfectSmooth = intersection.alpha < ALPHA_THRESHOLD;

    float3 value = 0.0f;

    if ( intersection.materialType != MATERIAL_TYPE_DIELECTRIC )
    { 
        bool hasLambertBrdf = false;
        bool hasCookTorranceBrdf = false;
        bool hasCookTorranceMultiscatteringBrdf = false;
        bool dielectricFresnel = false;
        float ratio_lambertBrdf = 0.f;
        float E = 0.f;
        float E_avg = 0.f;
        float3 F_ms = 0.f;

        bool hasAnyBrdf = !isInverted || intersection.isTwoSided;

        if ( intersection.multiscattering && ( intersection.materialType == MATERIAL_TYPE_PLASTIC || intersection.materialType == MATERIAL_TYPE_CONDUCTOR ) && hasAnyBrdf && !perfectSmooth )
        {
            E = SampleCookTorranceMicrofacetBRDFEnergyTexture( cosThetaO, intersection.alpha );
            E_avg = SampleCookTorranceMicrofacetBRDFAverageEnergyTexture( intersection.alpha );
        }

        if ( intersection.materialType == MATERIAL_TYPE_DIFFUSE && hasAnyBrdf )
        {
            hasLambertBrdf = true;

            ratio_lambertBrdf = 1.f;
        }
        else if ( intersection.materialType == MATERIAL_TYPE_PLASTIC && hasAnyBrdf )
        {
            hasLambertBrdf = true;
            hasCookTorranceBrdf = !perfectSmooth;
            hasCookTorranceMultiscatteringBrdf = intersection.multiscattering && !perfectSmooth;
            dielectricFresnel = true;

            ratio_lambertBrdf = 1.f - SpecularWeight( cosThetaO, intersection.alpha, intersection.ior.r );
            if ( hasCookTorranceMultiscatteringBrdf )
            {
                float F_avg = MultiscatteringFavgDielectric( intersection.ior.r );
                F_ms = MultiscatteringFresnel( E_avg, F_avg );
                ratio_lambertBrdf = max( ratio_lambertBrdf - F_ms * ( 1.f - E ), 0.f );
            }
        }
        else if ( intersection.materialType == MATERIAL_TYPE_CONDUCTOR && hasAnyBrdf && !perfectSmooth )
        {
            hasCookTorranceBrdf = true;
            hasCookTorranceMultiscatteringBrdf = intersection.multiscattering;
            dielectricFresnel = false;

            if ( hasCookTorranceMultiscatteringBrdf )
            {
                float3 k = intersection.albedo;
                float3 F_avg = MultiscatteringFavgConductor( intersection.ior, k );
                F_ms = MultiscatteringFresnel( E_avg, F_avg );
            }
        }
    
        if ( hasLambertBrdf )
        {
            value += EvaluateLambertBRDF( wi, wo, lightingContext ) * ratio_lambertBrdf * intersection.albedo;
        }
        if ( hasCookTorranceBrdf )
        {
            float3 brdfValue = EvaluateCookTorranceMircofacetBRDF( wi, wo, intersection.alpha, lightingContext );
            brdfValue *= dielectricFresnel ? (float3)FresnelDielectric( lightingContext.WOdotH, 1.f, intersection.ior.r ) : FresnelConductor( lightingContext.WOdotH, 1.f, intersection.ior, intersection.albedo );
            value += brdfValue;
        }
        if ( hasCookTorranceMultiscatteringBrdf )
        {
            value += EvaluateCookTorranceMultiscatteringBRDF( wi, wo, intersection.alpha, E, E_avg, F_ms, lightingContext );
        }
    }
    else
    {
        float etaO = isInverted ? intersection.ior.r : 1.0f;
        float etaI = isInverted ? 1.0f : intersection.ior.r;

        if ( !perfectSmooth )
        { 
            value += EvaluateCookTorranceMicrofacetBSDF( wi, wo, intersection.alpha, etaO, etaI, lightingContext );
        }

        if ( intersection.multiscattering && !perfectSmooth )
        {
            float eta = etaI / etaO;
            float inv_eta = etaO / etaI;

            float E_avg_enter = SampleCookTorranceMicrofacetBSDFAverageEnergyTexture( intersection.alpha, intersection.ior );
            float F_avg_enter = MultiscatteringFavgDielectric( intersection.ior );
            float E_avg_leave = SampleCookTorranceMicrofacetBSDFAverageEnergyTexture( intersection.alpha, 1.f / intersection.ior );
            float F_avg_leave = MultiscatteringFavgDielectric( 1.f / intersection.ior );
            float reciprocalFactor = ReciprocalFactor( F_avg_enter, F_avg_leave, E_avg_enter, E_avg_leave, 1.f / intersection.ior );

            float E = SampleCookTorranceMicrofacetBSDFEnergyTexture( cosThetaO, intersection.alpha, eta );
            float F_avg = isInverted ? F_avg_leave : F_avg_enter;
            float E_avg = isInverted ? E_avg_leave : E_avg_enter;
            float E_inv_avg = isInverted ? E_avg_enter : E_avg_leave;
            float ratio = ( isInverted ? 1.f - reciprocalFactor : reciprocalFactor ) * ( 1.f - F_avg );

            value += EvaluateCookTorranceMultiscatteringBSDF( wi, intersection.alpha, ratio, eta, inv_eta, E, E_avg, E_inv_avg );
        }
    }

    return value;
}

float EvaluateBSDFPdf( float3 wi, float3 wo, Intersection intersection )
{
    float3 biNormal = cross( intersection.normal, intersection.tangent );
    float3x3 tbn2world = float3x3( intersection.tangent, biNormal, intersection.normal );
    float3x3 world2tbn = transpose( tbn2world );

    wo = mul( wo, world2tbn );
    wi = mul( wi, world2tbn );

    bool isInverted = wo.z < 0.0f;
    if ( isInverted )
    {
        wo.z = -wo.z;
        wi.z = -wi.z;
    }

    float cosThetaO = wo.z;
    LightingContext lightingContext = LightingContextInit( wo, wi, isInverted );

    bool perfectSmooth = intersection.alpha < ALPHA_THRESHOLD;

    float pdf = 0.0f;

    if ( intersection.materialType != MATERIAL_TYPE_DIELECTRIC )
    { 
        bool hasLambertBrdf = false;
        bool hasCookTorranceBrdf = false;
        bool hasCookTorranceMultiscatteringBrdf = false;
        float weight_lambertBrdf = 0.f;
        float weight_cookTorranceBrdf = 0.f;
        float weight_cookTorranceMultiscatteringBrdf = 0.f;

        bool hasAnyBrdf = !isInverted || intersection.isTwoSided;

        if ( intersection.materialType == MATERIAL_TYPE_DIFFUSE && hasAnyBrdf )
        {
            hasLambertBrdf = true;

            weight_lambertBrdf = 1.f;
        }
        else if ( intersection.materialType == MATERIAL_TYPE_PLASTIC && hasAnyBrdf )
        {
            hasLambertBrdf = true;
            hasCookTorranceBrdf = !perfectSmooth;
            hasCookTorranceMultiscatteringBrdf = intersection.multiscattering && !perfectSmooth;

            weight_cookTorranceBrdf = SpecularWeight( cosThetaO, intersection.alpha, intersection.ior.r );
            weight_lambertBrdf = 1.f - weight_cookTorranceBrdf;
            if ( hasCookTorranceMultiscatteringBrdf )
            {
                float E = SampleCookTorranceMicrofacetBRDFEnergyTexture( cosThetaO, intersection.alpha );
                float E_avg = SampleCookTorranceMicrofacetBRDFAverageEnergyTexture( intersection.alpha );
                float F_avg = MultiscatteringFavgDielectric( intersection.ior.r );
                float F_ms = MultiscatteringFresnel( E_avg, F_avg );
                weight_cookTorranceMultiscatteringBrdf = F_ms * ( 1.f - E );
                weight_lambertBrdf = max( weight_lambertBrdf - weight_cookTorranceMultiscatteringBrdf, 0.f );
            }
        }
        else if ( intersection.materialType == MATERIAL_TYPE_CONDUCTOR && hasAnyBrdf && !perfectSmooth )
        {
            hasCookTorranceBrdf = true;
            hasCookTorranceMultiscatteringBrdf = intersection.multiscattering;

            weight_cookTorranceBrdf = 1.f;
            if ( hasCookTorranceMultiscatteringBrdf )
            {
                // Don't know the brdf energy for conductor, hence uniformly sample it.
                weight_cookTorranceBrdf = 0.5f;
                weight_cookTorranceMultiscatteringBrdf = 0.5f;
            }
        }
    
        if ( hasLambertBrdf )
        {
            pdf += EvaluateLambertBRDFPdf( wi, wo, lightingContext ) * weight_lambertBrdf;
        }
        if ( hasCookTorranceBrdf )
        { 
            pdf += EvaluateCookTorranceMicrofacetBRDFPdf( wi, wo, intersection.alpha, lightingContext ) * weight_cookTorranceBrdf;
        }
        if ( hasCookTorranceMultiscatteringBrdf )
        { 
            pdf += EvaluateCookTorranceMultiscatteringBRDFPdf( wi, wo, intersection.alpha, lightingContext ) * weight_cookTorranceMultiscatteringBrdf;
        }
    }
    else
    {
        bool hasCookTorranceBsdf = !perfectSmooth;
        bool hasCookTorranceMultiscatteringBsdf = intersection.multiscattering && !perfectSmooth;

        float weight_cookTorranceBsdf = 1.f;
        float weight_cookTorranceMultiscatteringBsdf = 0.f;

        float ratio = 0.f;

        float etaO = isInverted ? intersection.ior.r : 1.0f;
        float etaI = isInverted ? 1.0f : intersection.ior.r;
        float eta = 0.f;
        float inv_eta = 0.f;

        if ( hasCookTorranceMultiscatteringBsdf )
        {
            eta = etaI / etaO;
            inv_eta = etaO / etaI;

            float E_avg_enter = SampleCookTorranceMicrofacetBSDFAverageEnergyTexture( intersection.alpha, intersection.ior );
            float F_avg_enter = MultiscatteringFavgDielectric( intersection.ior );
            float E_avg_leave = SampleCookTorranceMicrofacetBSDFAverageEnergyTexture( intersection.alpha, 1.f / intersection.ior );
            float F_avg_leave = MultiscatteringFavgDielectric( 1.f / intersection.ior );
            float reciprocalFactor = ReciprocalFactor( F_avg_enter, F_avg_leave, E_avg_enter, E_avg_leave, 1.f / intersection.ior );

            float E = SampleCookTorranceMicrofacetBSDFEnergyTexture( cosThetaO, intersection.alpha, eta );
            float F_avg = isInverted ? F_avg_leave : F_avg_enter;
            ratio = ( isInverted ? 1.f - reciprocalFactor : reciprocalFactor ) * ( 1.f - F_avg );

            weight_cookTorranceBsdf = E;
            weight_cookTorranceMultiscatteringBsdf = 1.f - E;
        }

        if ( hasCookTorranceBsdf )
        {
            pdf += EvaluateCookTorranceMicrofacetBSDFPdf( wi, wo, intersection.alpha, etaO, etaI, lightingContext ) * weight_cookTorranceBsdf;
        }
        if ( hasCookTorranceMultiscatteringBsdf )
        {
            pdf += EvaluateCookTorranceMultiscatteringBSDFPdf( wi, intersection.alpha, ratio, eta, inv_eta ) * weight_cookTorranceMultiscatteringBsdf;
        }
    }

    return pdf;
}

void SampleBSDF( float3 wo
    , float2 BRDFSample
    , float BRDFSelectionSample
    , Intersection intersection
    , out float3 wi
    , out float3 value
    , out float pdf
    , out bool isDeltaBxdf )
{
    wi = 0.0f;
    value = 0.0f;
    pdf = 0.0f;
    isDeltaBxdf = false;

    float3 biNormal = cross( intersection.normal, intersection.tangent );
    float3x3 tbn2world = float3x3( intersection.tangent, biNormal, intersection.normal );
    float3x3 world2tbn = transpose( tbn2world );

    wo = mul( wo, world2tbn );

    bool isInverted = wo.z < 0.0f;
    if ( isInverted )
    {
        wo.z = -wo.z;
    }

    float cosThetaO = wo.z;
    LightingContext lightingContext = LightingContextInit( wo, isInverted );

    bool perfectSmooth = intersection.alpha < ALPHA_THRESHOLD;

    if ( intersection.materialType != MATERIAL_TYPE_DIELECTRIC )
    { 
        bool hasLambertBrdf = false;
        bool hasCookTorranceBrdf = false;
        bool hasCookTorranceMultiscatteringBrdf = false;
        
        bool dielectricFresnel = false;
        float weight_lambertBrdf = 0.f;
        float weight_cookTorranceBrdf = 0.f;
        float weight_cookTorranceMultiscatteringBrdf = 0.f;
        
        float E = 0.f;
        float E_avg = 0.f;
        float3 F_ms = 0.f;

        bool hasAnyBrdf = !isInverted || intersection.isTwoSided;

        if ( intersection.multiscattering && ( intersection.materialType == MATERIAL_TYPE_PLASTIC || intersection.materialType == MATERIAL_TYPE_CONDUCTOR ) && hasAnyBrdf )
        {
            E = SampleCookTorranceMicrofacetBRDFEnergyTexture( cosThetaO, intersection.alpha );
            E_avg = SampleCookTorranceMicrofacetBRDFAverageEnergyTexture( intersection.alpha );
        }

        if ( intersection.materialType == MATERIAL_TYPE_DIFFUSE && hasAnyBrdf )
        {
            hasLambertBrdf = true;

            weight_lambertBrdf = 1.f;
        }
        else if ( intersection.materialType == MATERIAL_TYPE_PLASTIC && hasAnyBrdf )
        {
            hasLambertBrdf = true;
            hasCookTorranceBrdf = true;
            hasCookTorranceMultiscatteringBrdf = intersection.multiscattering && !perfectSmooth;
            dielectricFresnel = true;

            weight_cookTorranceBrdf = SpecularWeight( cosThetaO, intersection.alpha, intersection.ior.r );
            weight_lambertBrdf = 1.f - weight_cookTorranceBrdf;
            if ( hasCookTorranceMultiscatteringBrdf )
            {
                float F_avg = MultiscatteringFavgDielectric( intersection.ior.r );
                F_ms = MultiscatteringFresnel( E_avg, F_avg );
                weight_cookTorranceMultiscatteringBrdf = F_ms * ( 1.f - E );
                weight_lambertBrdf = max( weight_lambertBrdf - weight_cookTorranceMultiscatteringBrdf, 0.f );
            }
        }
        else if ( intersection.materialType == MATERIAL_TYPE_CONDUCTOR && hasAnyBrdf )
        {
            hasCookTorranceBrdf = true;
            hasCookTorranceMultiscatteringBrdf = intersection.multiscattering && !perfectSmooth;
            dielectricFresnel = false;

            weight_cookTorranceBrdf = 1.f;
            if ( hasCookTorranceMultiscatteringBrdf )
            {
                float3 k = intersection.albedo;
                float3 F_avg = MultiscatteringFavgConductor( intersection.ior, k );
                F_ms = MultiscatteringFresnel( E_avg, F_avg );

                // Don't know the brdf energy for conductor, hence uniformly sample it.
                weight_cookTorranceBrdf = 0.5f;
                weight_cookTorranceMultiscatteringBrdf = 0.5f;
            }
        }
        
        if ( BRDFSelectionSample < weight_lambertBrdf )
        {
            SampleLambertBRDF( wo, BRDFSample, wi, lightingContext );
        }
        else if ( BRDFSelectionSample < weight_lambertBrdf + weight_cookTorranceBrdf )
        {
            if ( !perfectSmooth )
            {
                SampleCookTorranceMicrofacetBRDF( wo, BRDFSample, intersection.alpha, wi, lightingContext );
            }
            else
            {
                SampleSpecularBRDF( wo, wi, value.r, pdf, lightingContext );
                float3 F = dielectricFresnel ? (float3)FresnelDielectric( lightingContext.WOdotH, 1.f, intersection.ior.r ) 
                    : FresnelConductor( lightingContext.WOdotH, 1.f, intersection.ior, intersection.albedo );
                value = value.r * F;
                pdf *= weight_cookTorranceBrdf;

                isDeltaBxdf = true;
                hasLambertBrdf = false;
                hasCookTorranceBrdf = false;
                hasCookTorranceMultiscatteringBrdf = false;
            }
        }
        else /*if ( BRDFSelectionSample < weight_lambertBrdf + weight_cookTorranceBrdf + weight_cookTorranceMultiscatteringBrdf )*/ // Equals to 1
        {
            SampleCookTorranceMultiscatteringBRDF( wo, BRDFSample, intersection.alpha, wi, lightingContext );
        }

        if ( hasLambertBrdf )
        {
            value += EvaluateLambertBRDF( wi, wo, lightingContext ) * weight_lambertBrdf * intersection.albedo;
            pdf += EvaluateLambertBRDFPdf( wi, wo, lightingContext ) * weight_lambertBrdf;
        }
        if ( hasCookTorranceBrdf && !perfectSmooth )
        {
            float microfacetValue = EvaluateCookTorranceMircofacetBRDF( wi, wo, intersection.alpha, lightingContext );
            float3 F = dielectricFresnel ? (float3)FresnelDielectric( lightingContext.WOdotH, 1.f, intersection.ior.r ) : FresnelConductor( lightingContext.WOdotH, 1.f, intersection.ior, intersection.albedo );
            value += microfacetValue * F;
            pdf += EvaluateCookTorranceMicrofacetBRDFPdf( wi, wo, intersection.alpha, lightingContext ) * weight_cookTorranceBrdf;
        }
        if ( hasCookTorranceMultiscatteringBrdf )
        {
            value += EvaluateCookTorranceMultiscatteringBRDF( wi, wo, intersection.alpha, E, E_avg, F_ms, lightingContext );
            pdf += EvaluateCookTorranceMultiscatteringBRDFPdf( wi, wo, intersection.alpha, lightingContext ) * weight_cookTorranceMultiscatteringBrdf;
        }
    }
    else
    {
        bool hasCookTorranceBsdf = true;
        bool hasCookTorranceMultiscatteringBsdf = intersection.multiscattering && !perfectSmooth;

        float weight_cookTorranceBsdf = 1.f;
        float weight_cookTorranceMultiscatteringBsdf = 0.f;

        float E = 0.f;
        float E_avg = 0.f;
        float E_inv_avg = 0.f;
        float ratio = 0.f;

        float etaO = isInverted ? intersection.ior.r : 1.0f;
        float etaI = isInverted ? 1.0f : intersection.ior.r;
        float eta = 0.f;
        float inv_eta = 0.f;

        if ( hasCookTorranceMultiscatteringBsdf )
        {
            eta = etaI / etaO;
            inv_eta = etaO / etaI;

            float E_avg_enter = SampleCookTorranceMicrofacetBSDFAverageEnergyTexture( intersection.alpha, intersection.ior );
            float F_avg_enter = MultiscatteringFavgDielectric( intersection.ior );
            float E_avg_leave = SampleCookTorranceMicrofacetBSDFAverageEnergyTexture( intersection.alpha, 1.f / intersection.ior );
            float F_avg_leave = MultiscatteringFavgDielectric( 1.f / intersection.ior );
            float reciprocalFactor = ReciprocalFactor( F_avg_enter, F_avg_leave, E_avg_enter, E_avg_leave, 1.f / intersection.ior );

            E = SampleCookTorranceMicrofacetBSDFEnergyTexture( cosThetaO, intersection.alpha, eta );
            float F_avg = isInverted ? F_avg_leave : F_avg_enter;
            E_avg = isInverted ? E_avg_leave : E_avg_enter;
            E_inv_avg = isInverted ? E_avg_enter : E_avg_leave;
            ratio = ( isInverted ? 1.f - reciprocalFactor : reciprocalFactor ) * ( 1.f - F_avg );

            weight_cookTorranceBsdf = E;
            weight_cookTorranceMultiscatteringBsdf = 1.f - E;
        }

        if ( !perfectSmooth )
        {
            if ( BRDFSelectionSample < weight_cookTorranceBsdf )
            { 
                SampleCookTorranceMicrofacetBSDF( wo, BRDFSelectionSample, BRDFSample, intersection.alpha, etaO, etaI, wi, lightingContext );
            }
            else /*if ( BRDFSelectionSample < weight_cookTorranceBsdf + weight_cookTorranceMultiscatteringBsdf )*/ // Equals 1
            {
                SampleCookTorranceMultiscatteringBSDF( wo, BRDFSelectionSample, BRDFSample, intersection.alpha, ratio, eta, inv_eta, wi, lightingContext );
            }
        }
        else
        {
            SampleSpecularBSDF( wo, BRDFSelectionSample, etaO, etaI, wi, value.r, pdf, lightingContext );
            value = value.r;
            pdf *= weight_cookTorranceBsdf;

            isDeltaBxdf = true;
            hasCookTorranceBsdf = false;
            hasCookTorranceMultiscatteringBsdf = false;
        }

        if ( hasCookTorranceBsdf )
        {
            value += EvaluateCookTorranceMicrofacetBSDF( wi, wo, intersection.alpha, etaO, etaI, lightingContext );
            pdf += EvaluateCookTorranceMicrofacetBSDFPdf( wi, wo, intersection.alpha, etaO, etaI, lightingContext ) * weight_cookTorranceBsdf;
        }
        if ( hasCookTorranceMultiscatteringBsdf )
        {
            value += EvaluateCookTorranceMultiscatteringBSDF( wi, intersection.alpha, ratio, eta, inv_eta, E, E_avg, E_inv_avg );
            pdf += EvaluateCookTorranceMultiscatteringBSDFPdf( wi, intersection.alpha, ratio, eta, inv_eta ) * weight_cookTorranceMultiscatteringBsdf;
        }
    }

    if ( isInverted )
    {
        wi.z = -wi.z;
    }

    wi = mul( wi, tbn2world );
}

#endif