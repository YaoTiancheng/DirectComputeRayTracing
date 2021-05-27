#ifndef _FRESNEL_H_
#define _FRESNEL_H_

float EvaluateDielectricFresnel( float cosThetaI, float etaI, float etaT )
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

#endif