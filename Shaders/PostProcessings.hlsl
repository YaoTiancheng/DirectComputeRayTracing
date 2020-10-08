
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

static const float  LUM_WHITE_2 = 2.f;

// See Automatic Exposure Using a Luminance Histogram, https://bruop.github.io/exposure/
// Also see Lagard and de Rousiers, 2014 (pg. 85), https://media.contentapi.ea.com/content/dam/eacom/frostbite/files/course-notes-moving-frostbite-to-pbr-v2.pdf
float3 ApplyExposure( float avgLum, float3 color )
{
    return color / ( avgLum * 9.6f );
}

float3 ReinhardTonemap( float3 color )
{
    return color * ( 1.0f + color / LUM_WHITE_2 ) / ( 1.0f + color );
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
    c.xyz = ApplyExposure( avgLum, c.xyz );
    c.xyz = ReinhardTonemap( c.xyz );

    c.xyz = pow( c.xyz, 0.45f );

    return float4( c.xyz, 1.0f );
}