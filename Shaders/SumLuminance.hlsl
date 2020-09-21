
#include "SumLuminanceDef.inc.hlsl"

#if defined( REDUCE_TO_1D )
#define BLOCKSIZE    SL_BLOCKSIZE
#define BLOCKSIZEY   SL_BLOCKSIZEY
#define groupthreads SL_REDUCE_TO_1D_GROUPTHREADS
Texture2D g_Input               : register( t0 );
#else
#define groupthreads SL_REDUCE_TO_SINGLE_GROUPTHREADS
StructuredBuffer<float> g_Input : register( t0 );
#endif

RWStructuredBuffer<float> g_Result  : register( u0 );

cbuffer Constants : register( b0 )
{
    uint4 g_Param;
};

groupshared float gs_accum[ groupthreads ];

#if defined( REDUCE_TO_1D )

static const float4 LUM_VECTOR = float4( .299, .587, .114, 0 );

[numthreads( BLOCKSIZE, BLOCKSIZEY, 1 )]
void main( uint3 Gid : SV_GroupID, uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint GI : SV_GroupIndex )
{
    float4 s = g_Input.Load( uint3( DTid.xy, 0 ) ) +
               g_Input.Load( uint3( DTid.xy + uint2( BLOCKSIZE * g_Param.x, 0 ), 0 ) ) +
               g_Input.Load( uint3( DTid.xy + uint2( 0, BLOCKSIZEY * g_Param.y ), 0 ) ) +
               g_Input.Load( uint3( DTid.xy + uint2( BLOCKSIZE * g_Param.x, BLOCKSIZEY * g_Param.y ), 0 ) );
    s.xyz /= s.w;

    gs_accum[ GI ] = dot( s, LUM_VECTOR );

    // Parallel reduction algorithm follows 
    GroupMemoryBarrierWithGroupSync();
    if ( GI < 32 )
        gs_accum[ GI ] += gs_accum[ 32 + GI ];

    GroupMemoryBarrierWithGroupSync();
    if ( GI < 16 )
        gs_accum[ GI ] += gs_accum[ 16 + GI ];

    GroupMemoryBarrierWithGroupSync();
    if ( GI < 8 )
        gs_accum[ GI ] += gs_accum[ 8 + GI ];

    GroupMemoryBarrierWithGroupSync();
    if ( GI < 4 )
        gs_accum[ GI ] += gs_accum[ 4 + GI ];

    GroupMemoryBarrierWithGroupSync();
    if ( GI < 2 )
        gs_accum[ GI ] += gs_accum[ 2 + GI ];

    GroupMemoryBarrierWithGroupSync();
    if ( GI < 1 )
        gs_accum[ GI ] += gs_accum[ 1 + GI ];

    if ( GI == 0 )
    {
        g_Result[ Gid.y * g_Param.x + Gid.x ] = gs_accum[ 0 ];
    }
}

#else 

[numthreads( groupthreads, 1, 1 )]
void main( uint3 Gid : SV_GroupID, uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint GI : SV_GroupIndex )
{
    if ( DTid.x < g_Param.x )
        gs_accum[ GI ] = g_Input[ DTid.x ];
    else
        gs_accum[ GI ] = 0;

    // Parallel reduction algorithm follows 
    GroupMemoryBarrierWithGroupSync();
    if ( GI < 64 )
        gs_accum[ GI ] += gs_accum[ 64 + GI ];

    GroupMemoryBarrierWithGroupSync();
    if ( GI < 32 )
        gs_accum[ GI ] += gs_accum[ 32 + GI ];

    GroupMemoryBarrierWithGroupSync();
    if ( GI < 16 )
        gs_accum[ GI ] += gs_accum[ 16 + GI ];

    GroupMemoryBarrierWithGroupSync();
    if ( GI < 8 )
        gs_accum[ GI ] += gs_accum[ 8 + GI ];

    GroupMemoryBarrierWithGroupSync();
    if ( GI < 4 )
        gs_accum[ GI ] += gs_accum[ 4 + GI ];

    GroupMemoryBarrierWithGroupSync();
    if ( GI < 2 )
        gs_accum[ GI ] += gs_accum[ 2 + GI ];

    GroupMemoryBarrierWithGroupSync();
    if ( GI < 1 )
        gs_accum[ GI ] += gs_accum[ 1 + GI ];

    if ( GI == 0 )
    {
        g_Result[ Gid.x ] = gs_accum[ 0 ];
    }
}

#endif