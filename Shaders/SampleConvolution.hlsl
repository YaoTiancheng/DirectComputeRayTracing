
#define PI 3.1415926535

cbuffer Constants : register( b0 )
{
    uint2 g_Resolution;
    float g_FilterRadius;
    float g_GaussianAlpha;
    float g_GaussianExp;
    float g_MitchellFactor0;
    float g_MitchellFactor1;
    float g_MitchellFactor2;
    float g_MitchellFactor3;
    float g_MitchellFactor4;
    float g_MitchellFactor5;
    float g_MitchellFactor6;
    uint g_LanczosTau;
}

Texture2D<float2> g_SamplePositionTexture : register( t0 );
Texture2D<float4> g_SampleValueTexture : register( t1 );
RWTexture2D<float4> g_FilmTexture : register( u0 );

float Gaussian( float d )
{
    return max( 0.0f, exp( -g_GaussianAlpha * d * d ) - g_GaussianExp );
}

float Mitchell1D( float x )
{
    x = abs( 2 * x );
    float result = x < 1
        ? g_MitchellFactor4 * x * x * x + g_MitchellFactor5 * x * x + g_MitchellFactor6
        : ( x < 2 ? g_MitchellFactor0 * x * x * x + g_MitchellFactor1 * x * x + g_MitchellFactor2 * x + g_MitchellFactor3 : 0 );
    result *= 1.f / 6.f;
    return result;
}

float Sinc( float x )
{
    x = abs( x );
    return x >= 1e-5 ? sin( PI * x ) / ( PI * x ) : 1;
}

float WindowedSinc( float x, float radius )
{
    x = abs( x );
    float lanczos = Sinc( x / g_LanczosTau );
    return x > radius ? 0 : Sinc( x ) * lanczos;
}

float EvaluateFilter( float filterRadius, float2 p )
{
#if defined( FILTER_TRIANGLE )
    return max( 0.0f, filterRadius - abs( p.x ) ) * max( 0.0f, filterRadius - abs( p.y ) );
#elif defined( FILTER_GAUSSIAN )
    return Gaussian( p.x ) * Gaussian( p.y );
#elif defined( FILTER_MITCHELL )
    return Mitchell1D( p.x / g_FilterRadius ) * Mitchell1D( p.y / g_FilterRadius );
#elif defined( FILTER_LANCZOS_SINC )
    return WindowedSinc( p.x, g_FilterRadius ) * WindowedSinc( p.y, g_FilterRadius );
#elif defined( FILTER_BOX )
    return all( abs( p ) <= filterRadius ) ? 1.0f : 0.0f;
#endif
}

void AddSample( uint2 pixelPos
    , float filterRadius
    , uint2 filmResolution
    , Texture2D<float2> samplePositionTexture
    , Texture2D<float4> sampleValueTexture
    , RWTexture2D<float4> filmTexture )
{
    float filterWeightSum = 0.0f;
    float3 sampleValueSum = 0.0f;

    float2 pixelCenter = pixelPos + 0.5f;
    int xStart = max( 0, (int)floor( pixelCenter.x - filterRadius ) );
    int xEnd = min( filmResolution.x - 1, (int)floor( pixelCenter.x + filterRadius ) );
    int yStart = max( 0, (int)floor( pixelCenter.y - filterRadius ) );
    int yEnd = min( filmResolution.y - 1, (int)floor( pixelCenter.y + filterRadius ) );
    for ( int y = yStart; y <= yEnd; ++y )
    {
        for ( int x = xStart; x <= xEnd; ++x )
        {
            float2 samplePosition = samplePositionTexture[ uint2( x, y ) ];
            samplePosition += uint2( x, y );
            float3 sampleValue = sampleValueTexture[ uint2( x, y ) ].xyz;
            float2 offset = pixelCenter - samplePosition;
            float filterWeight = EvaluateFilter( filterRadius, offset );
            sampleValueSum += filterWeight * sampleValue;
            filterWeightSum += filterWeight;
        }
    }

    float4 v = filmTexture[ pixelPos ];
    v.xyz += sampleValueSum;
    v.w += filterWeightSum;
    filmTexture[ pixelPos ] = v;
}

[ numthreads( 8, 8, 1 ) ]
void main( uint2 pixelPos : SV_DispatchThreadID )
{
    AddSample( pixelPos, g_FilterRadius, g_Resolution, g_SamplePositionTexture, g_SampleValueTexture, g_FilmTexture );
}