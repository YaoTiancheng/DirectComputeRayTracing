#ifndef _LIGHTINGCONTEXT_H_
#define _LIGHTINGCONTEXT_H_

struct LightingContext
{
    float WOdotN;
    float WIdotN;
    float3 H;
};

void LightingContextAssignH( float3 wo, float3 wi, inout LightingContext lightingContext )
{
    lightingContext.H = wi + wo;
    lightingContext.H = all( lightingContext.H == 0.0f ) ? 0.0f : normalize( lightingContext.H );
}

LightingContext LightingContextInit( float3 wo, float3 wi )
{
    LightingContext context;
    context.WOdotN = wo.z;
    context.WIdotN = wi.z;
    LightingContextAssignH( wo, wi, context );
    return context;
}

LightingContext LightingContextInit( float3 wo )
{
    LightingContext context = (LightingContext)0;
    context.WOdotN = wo.z;
    return context;
}

#endif