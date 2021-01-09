
cbuffer PostProcessingConstants : register( b0 )
{
    float4 g_Params;
}

struct VertexOut
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD;
};

Texture2D               g_FilmTexture     : register( t0 );
StructuredBuffer<float> g_LuminanceBuffer : register( t1 );

SamplerState LinearSampler;
SamplerState PointSampler;

float ComputeEV100FromAverageLuminance( float avgLum )
{
    return log2( avgLum * 100.0f / 12.5f );
}

float ConvertEV100ToExposure( float EV100 )
{
    float maxLuminance = 1.2f * pow( 2.0f, EV100 );
    return 1.0f / maxLuminance;
}

// See Automatic Exposure Using a Luminance Histogram, https://bruop.github.io/exposure/
// Also see Lagard and de Rousiers, 2014 (pg. 85), https://media.contentapi.ea.com/content/dam/eacom/frostbite/files/course-notes-moving-frostbite-to-pbr-v2.pdf
float ComputeExposure( float avgLum )
{
    float EV100 = ComputeEV100FromAverageLuminance( avgLum );
    return ConvertEV100ToExposure( EV100 );
}

float3 ReinhardTonemap( float3 color )
{
    return color * ( 1.0f + color / g_Params.y ) / ( 1.0f + color );
}

VertexOut MainVS( float4 pos : POSITION )
{
    VertexOut o = ( VertexOut ) 0.0f;
    o.position = pos;
    o.texcoord = ( float2( pos.x, -pos.y ) + 1.0f ) * 0.5f;
    return o;
}

#if !defined( COPY )
float4 MainPS( VertexOut i ) : SV_TARGET
{
    float4 c = g_FilmTexture.Sample( PointSampler, i.texcoord );
    c.xyz /= c.w;

#if !defined( DISABLE_POST_FX )
    float avgLum = exp( g_LuminanceBuffer[ 0 ] * g_Params.x );
    float exposure = ComputeExposure( avgLum );
    c.xyz *= exposure;
    c.xyz = ReinhardTonemap( c.xyz );
#endif

    c.xyz = pow( c.xyz, 0.45f );

    return float4( c.xyz, 1.0f );
}
#else
float4 MainPS( VertexOut i ) : SV_TARGET
{
    float4 c = g_FilmTexture.Sample( LinearSampler, i.texcoord );
    return c;
}
#endif