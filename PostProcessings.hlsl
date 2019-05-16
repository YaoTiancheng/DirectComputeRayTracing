
struct VertexOut
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD;
};

Texture2D g_FilmTexture;

SamplerState CopySampler;

VertexOut ScreenQuadMainVS( float4 pos : POSITION )
{
    VertexOut o = ( VertexOut ) 0.0f;
    o.position = pos;
    o.texcoord = ( float2( pos.x, -pos.y ) + 1.0f ) * 0.5f;
    return o;
}

float4 CopyMainPS( VertexOut i ) : SV_TARGET
{
    float4 c = g_FilmTexture.Sample( CopySampler, i.texcoord );
    return float4( c.xyz / c.w, 1.0f );
}