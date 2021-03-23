#ifndef _LIGHTSAMPLING_H_
#define _LIGHTSAMPLING_H_

void SamplePointLight( SLight light, float2 samples, float3 position, out float3 l, out float3 wi, out float distance, out float pdf )
{
    float3 lightPosWS = light.transform._41_42_43;
    wi = lightPosWS - position;
    distance = length( wi );
    wi /= distance;
    l = light.color / ( distance * distance );
    pdf = 1.0f;
}

void SampleRectangleLight( SLight light, float2 samples, float3 position, out float3 l, out float3 wi, out float distance, out float pdf )
{
    float2 lightPosLS = samples - 0.5f; // Map to [-0.5, 0.5]
    float3 lightPosWS = mul( float4( lightPosLS, 0.0f, 1.0f ), light.transform ).xyz;
    float3 normalWS = light.transform._31_32_33;
    wi = lightPosWS - position;
    distance = length( wi );
    wi /= distance;

    float WIdotN = dot( wi, normalWS );
    l = WIdotN > 0.0f ? light.color : 0.0f;

    float2 size = float2( length( light.transform._11_12_13 ), length( light.transform._21_22_23 ) ); // This matrix shouldn't contain shear or negative scaling, otherwise this is wrong!
    float area = size.x * size.y;
    float3 pVec = position - lightPosWS;
    pdf = dot( pVec, pVec ) / ( WIdotN * area );
}

#endif