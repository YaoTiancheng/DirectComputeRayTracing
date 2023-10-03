#include "CookTorranceBSDF.inc.hlsl"
#include "SpecularBxDF.inc.hlsl"
#include "Xoshiro.inc.hlsl"
#include "Samples.inc.hlsl"
#include "LightingContext.inc.hlsl"

#define ALPHA_THRESHOLD 0.00052441

#if defined( INTEGRATE_COOKTORRANCE_BRDF )

cbuffer Constants : register( b0 )
{
	uint g_RandomSeed;
	uint g_Initialize;
}

RWTexture2D<float> g_OutputTexture : register( u0 );

[ numthreads( GROUP_SIZE_X, GROUP_SIZE_Y, 1 ) ]
void main( uint2 threadId : SV_DispatchThreadID )
{
	const float cosThetaO = max( threadId.x * LUT_INTERVAL_X, 0.0001f );
	const float alpha = threadId.y * LUT_INTERVAL_Y;
	const uint sampleCount = SAMPLE_COUNT;
	const double sampleWeight = SAMPLE_WEIGHT;
	const uint randomSeed = g_RandomSeed;

	const bool perfectSmooth = alpha < ALPHA_THRESHOLD;

	Xoshiro128StarStar rng = InitializeRandomNumberGenerator( uint2( 0, 0 ), randomSeed );

	double result = g_Initialize == 0 ? (double)g_OutputTexture.Load( threadId ) : 0.0;
	for ( uint sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex )
	{
		float3 wo = float3( sqrt( 1.0 - cosThetaO * cosThetaO ), 0.0, cosThetaO );
		float3 wi;
		LightingContext lightingContext = (LightingContext)0;

		float sampleValue, samplePdf;
		if ( perfectSmooth )
		{
			SampleSpecularBRDF( wo, wi, sampleValue, samplePdf, lightingContext );
		}
		else
		{
			float2 sample = GetNextSample2D( rng );
			SampleCookTorranceMicrofacetBRDF( wo, sample, alpha, wi, lightingContext );
			sampleValue = EvaluateCookTorranceMircofacetBRDF( wi, wo, alpha, lightingContext );
			samplePdf = EvaluateCookTorranceMicrofacetBRDFPdf( wi, wo, alpha, lightingContext );
		}

		if ( samplePdf > 0.f )
		{ 
			result += sampleWeight * sampleValue * abs( wi.z ) / samplePdf;
		}
	}

	g_OutputTexture[ threadId ] = (float)result;
}

#endif

#if defined( INTEGRATE_AVERAGE )

// Trapezoidal composition

Texture2D<float> g_SrcTexture : register( t0 );
RWTexture2D<unorm float> g_DestTexture : register( u0 );

[ numthreads( GROUP_SIZE_X, 1, 1 ) ]
void main( uint threadId : SV_DispatchThreadID )
{
	const uint sampleCount = GROUP_SIZE_Y;
	const uint n = sampleCount - 1;
	double fa = g_SrcTexture[ uint2( 0, threadId ) ] * 0.0001; // cosTheta clamped to 0.0001
	double sum = 0;
	for ( uint i = 1; i < n; ++i )
	{
		const double cosTheta = i * LUT_INTERVAL_X; // LUT_INTERVAL_X is cosTheta interval
		sum += saturate( g_SrcTexture[ uint2( i, threadId ) ] ) * cosTheta;
	}
	double fb = g_SrcTexture[ uint2( n, threadId ) ]; // cosTheta is 1
	double result = ( sum + ( fa + fb ) * 0.5 ) * ( 1.0 / n );
	g_DestTexture[ uint2( threadId, 0 ) ] = result * 2.0;
}

#endif

#if defined( COPY )

Texture2D<float> g_SrcTexture : register( t0 );
RWTexture2D<unorm float> g_DestTexture : register( u0 );

[ numthreads( GROUP_SIZE_X, GROUP_SIZE_Y, 1 ) ]
void main( uint2 threadId : SV_DispatchThreadID )
{
	g_DestTexture[ threadId ] = g_SrcTexture[ threadId ];
}

#endif
