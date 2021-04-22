#ifndef _FRESNEL_H_
#define _FRESNEL_H_

float EvaluateDielectricFresnel( float WOdotM, float etaI, float etaT )
{
    float cosThetaI = WOdotM;
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