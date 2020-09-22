
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

SamplerState CopySampler;

static const float  MIDDLE_GRAY = 0.03f;
static const float  LUM_WHITE = 0.7f;

float3 ReinhardTonemap( float avgLum, float3 color )
{
    float3 outColor = color * MIDDLE_GRAY / ( avgLum + 0.001f );
    outColor *= ( 1.0f + outColor / LUM_WHITE );
    outColor /= ( 1.0f + outColor );
    return outColor;
}

VertexOut MainVS( float4 pos : POSITION )
{
    VertexOut o = ( VertexOut ) 0.0f;
    o.position = pos;
    o.texcoord = ( float2( pos.x, -pos.y ) + 1.0f ) * 0.5f;
    return o;
}

float4 MainPS( VertexOut i ) : SV_TARGET
{
    float4 c = g_FilmTexture.Sample( CopySampler, i.texcoord );
    c.xyz /= c.w;

    float avgLum = g_LuminanceBuffer[ 0 ] * g_Params.x;
    c.xyz = ReinhardTonemap( avgLum, c.xyz );

    c.xyz = pow( c.xyz, 0.45f );

    return float4( c.xyz, 1.0f );
}