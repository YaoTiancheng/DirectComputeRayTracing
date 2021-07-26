#ifndef _LIGHTINGCONTEXT_H_
#define _LIGHTINGCONTEXT_H_

struct LightingContext
{
    float3 H;
    float WOdotH;
};

void LightingContextCalculateH( float3 wo, float3 wi, inout LightingContext lightingContext )
{
    lightingContext.H = wi + wo;
    lightingContext.H = all( lightingContext.H == 0.0f ) ? 0.0f : normalize( lightingContext.H );
    lightingContext.WOdotH = dot( lightingContext.H, wo );
}

void LightingContextAssignH( float3 wo, float3 h, inout LightingContext lightingContext )
{
    lightingContext.H = h;
    lightingContext.WOdotH = dot( h, wo );
}

LightingContext LightingContextInit( float3 wo, float3 wi )
{
    LightingContext context;
    LightingContextCalculateH( wo, wi, context );
    return context;
}

LightingContext LightingContextInit( float3 wo )
{
    LightingContext context = (LightingContext)0;
    return context;
}

#endif