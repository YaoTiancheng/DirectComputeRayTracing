
cbuffer PostProcessingConstants : register( b0 )
{
    float g_ReciprocalPixelCount;
    float g_MaxWhiteSqr;
    float g_TexcoordScale;
    float g_EV100;
}

struct VertexOut
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD;
};

Texture2D               g_FilmTexture     : register( t0 );
StructuredBuffer<float> g_LuminanceBuffer : register( t1 );

SamplerState LinearSampler : register( s0 );
SamplerState PointSampler : register( s1 );

float ComputeEV100FromAverageLuminance( float avgLum )
{
    return log2( avgLum * 100.0f / 12.5f );
}

float ConvertEV100ToExposure( float EV100 )
{
    float maxLuminance = 1.2f * pow( 2.0f, EV100 );
    return 1.0f / maxLuminance;
}

float3 ReinhardTonemap( float3 color )
{
    return color * ( 1.0f + color / g_MaxWhiteSqr ) / ( 1.0f + color );
}

VertexOut MainVS( float4 pos : POSITION )
{
    VertexOut o = ( VertexOut ) 0.0f;
    o.position = pos;
    o.texcoord = ( float2( pos.x, -pos.y ) + 1.0f ) * 0.5f;
    o.texcoord *= g_TexcoordScale;
    return o;
}

#if !defined( COPY )
float4 MainPS( VertexOut i ) : SV_TARGET
{
    float4 c = g_FilmTexture.Sample( PointSampler, i.texcoord );
    c.xyz /= c.w;

#if !defined( DISABLE_POST_FX )
    float EV100;
#if defined( AUTO_EXPOSURE )
    float avgLum = exp( g_LuminanceBuffer[ 0 ] * g_ReciprocalPixelCount );
    EV100 = ComputeEV100FromAverageLuminance( avgLum );
#else
    EV100 = g_EV100;
#endif
    float exposure = ConvertEV100ToExposure( EV100 );
    c.xyz *= exposure;
    c.xyz = ReinhardTonemap( c.xyz );
#endif

    return float4( c.xyz, 1.0f );
}
#else
float4 MainPS( VertexOut i ) : SV_TARGET
{
    float4 c = g_FilmTexture.Sample( LinearSampler, i.texcoord );
    return c;
}
#endif