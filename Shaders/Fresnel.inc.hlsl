#ifndef _FRESNEL_H_
#define _FRESNEL_H_

float FresnelDielectric( float cosThetaI, float etaI, float etaT )
{
    cosThetaI = clamp( cosThetaI, -1.0f, 1.0f );

    if ( cosThetaI < 0.0f )
    {
        float etaTemp = etaI;
        etaI = etaT;
        etaT = etaTemp;
        cosThetaI = -cosThetaI;
    }

    float sinThetaI = sqrt( 1.0f - cosThetaI * cosThetaI );
    float sinThetaT = etaI / etaT * sinThetaI;
    if ( sinThetaT >= 1.0f )
    {
        return 1.0f;
    }
    float cosThetaT = sqrt( 1.0f - sinThetaT * sinThetaT );
    float Rparl = ( ( etaT * cosThetaI ) - ( etaI * cosThetaT ) ) /
        ( ( etaT * cosThetaI ) + ( etaI * cosThetaT ) );
    float Rperp = ( ( etaI * cosThetaI ) - ( etaT * cosThetaT ) ) /
        ( ( etaI * cosThetaI ) + ( etaT * cosThetaT ) );
    return ( Rparl * Rparl + Rperp * Rperp ) * 0.5f;
}

float3 FresnelConductor( float cosThetaI, float3 etaI, float3 etaT, float3 k )
{
    cosThetaI = clamp( cosThetaI, -1, 1 );
    float3 eta = etaT / etaI;
    float3 etak = k / etaI;

    float cosThetaI2 = cosThetaI * cosThetaI;
    float sinThetaI2 = 1.0f - cosThetaI2;
    float3 eta2 = eta * eta;
    float3 etak2 = etak * etak;

    float3 t0 = eta2 - etak2 - sinThetaI2;
    float3 a2plusb2 = SafeSqrt( t0 * t0 + 4 * eta2 * etak2 );
    float3 t1 = a2plusb2 + cosThetaI2;
    float3 a = SafeSqrt( 0.5f * ( a2plusb2 + t0 ) ); // Might get negetive number due to rounding error, so use SafeSqrt to avoid NaN
    float3 t2 = 2.0f * cosThetaI * a;
    float3 Rs = ( t1 - t2 ) / ( t1 + t2 );

    float3 t3 = cosThetaI2 * a2plusb2 + sinThetaI2 * sinThetaI2;
    float3 t4 = t2 * sinThetaI2;
    float3 Rp = Rs * ( t3 - t4 ) / ( t3 + t4 );

    return 0.5 * ( Rp + Rs );
}

float3 FresnelSchlick( float cosThetaI, float3 F0 )
{
    cosThetaI = clamp( cosThetaI, -1, 1 );
    float a = 1 - cosThetaI;
    float a2 = a * a;
    float a4 = a2 * a2;
    float a5 = a4 * a;
    return F0 + ( 1 - F0 ) * a5;
}

#endif