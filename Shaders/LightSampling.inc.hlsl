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

float EvaluateRectangleLightPdf( SLight light, float3 wi, float3 position, out float distance )
{
    float3 normalWS = light.transform._31_32_33;
    float3 lightPosWS = light.transform._41_42_43;
    float2 size = float2( length( light.transform._11_12_13 ), length( light.transform._21_22_23 ) ); // This matrix shouldn't contain shear or negative scaling, otherwise this is wrong!

    float WIdotN = -dot( wi, normalWS );
    distance = dot( lightPosWS - position, normalWS ) / ( -WIdotN ); // Ray-plane intersection
    float3 hitPosWS = position + wi * distance;
    float3 hitPosVec = hitPosWS - lightPosWS;
    float2 hitPosLS = float2( dot( hitPosVec, light.transform._11_12_13 ), dot( hitPosVec, light.transform._21_22_23 ) );
           hitPosLS /= size * size;
    float area = size.x * size.y;
    return WIdotN > 0.0f && distance > 0.0f && all( abs( hitPosLS ) <= 0.5f )
        ? ( distance * distance ) / ( WIdotN * area )
        : 0.0f;
}

void SampleRectangleLight( SLight light, float2 samples, float3 position, out float3 l, out float3 wi, out float distance, out float pdf )
{
    float3 normalWS = light.transform._31_32_33;

    float2 samplePosLS = samples - 0.5f; // Map to [-0.5, 0.5]
    float3 samplePosWS = mul( float4( samplePosLS, 0.0f, 1.0f ), light.transform ).xyz;
    wi = samplePosWS - position;
    distance = length( wi );
    wi /= distance;

    float WIdotN = -dot( wi, normalWS );
    l = WIdotN > 0.0f ? light.color : 0.0f;

    float2 size = float2( length( light.transform._11_12_13 ), length( light.transform._21_22_23 ) ); // This matrix shouldn't contain shear or negative scaling, otherwise this is wrong!
    float area = size.x * size.y;
    pdf = WIdotN > 0.0f ? ( distance * distance ) / ( WIdotN * area ) : 0.0f;
}

#endif