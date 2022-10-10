#ifndef _BVHACCEL_H_
#define _BVHACCEL_H_

#include "RayPrimitiveIntersect.inc.hlsl"

uint BVHNodeGetPrimitiveCount( BVHNode node )
{
    return ( node.misc & 0xFF );
}

uint BVHNodeHasBLAS( BVHNode node )
{
    return ( node.misc & 0x100 ) != 0;
}

uint BVHNodeGetSplitAxis( BVHNode node )
{
    return ( node.misc >> 9 ) & 0x3;
}

struct BVHTraversalStack
{
    uint nodeIndex[ RT_BVH_TRAVERSAL_STACK_SIZE ];
    int count;
};

groupshared BVHTraversalStack gs_BVHTraversalStacks[ RT_BVH_TRAVERSAL_GROUP_SIZE ]; // Should match group size

void BVHTraversalStackUnpackedNodeIndex( uint packedNodeIndex, out uint unpackedNodeIndex, out bool isBLAS )
{
    unpackedNodeIndex = packedNodeIndex & 0x7FFFFFFF;
    isBLAS = ( packedNodeIndex & 0x80000000 ) != 0;
}

uint BVHTraversalStackPackNodeIndex( uint nodeIndex, bool isBLAS )
{
    return ( nodeIndex & 0x7FFFFFFF ) | ( isBLAS ? 0x80000000 : 0 );
}

bool BVHTraversalStackPopback( uint dispatchThreadIndex, out uint nodeIndex, out bool isBLAS )
{
    bool isEmpty = ( gs_BVHTraversalStacks[ dispatchThreadIndex ].count == 0 );
    uint packedNodeIndex = isEmpty ? 0 : gs_BVHTraversalStacks[ dispatchThreadIndex ].nodeIndex[ --gs_BVHTraversalStacks[ dispatchThreadIndex ].count ];
    BVHTraversalStackUnpackedNodeIndex( packedNodeIndex, nodeIndex, isBLAS );
    return !isEmpty;
}

void BVHTraversalStackPushback( uint dispatchThreadIndex, uint nodeIndex, bool isBLAS )
{
    uint packedNodeIndex = BVHTraversalStackPackNodeIndex( nodeIndex, isBLAS );
    gs_BVHTraversalStacks[ dispatchThreadIndex ].nodeIndex[ gs_BVHTraversalStacks[ dispatchThreadIndex ].count++ ] = packedNodeIndex;
}

void BVHTraversalStackReset( uint dispatchThreadIndex )
{
    gs_BVHTraversalStacks[ dispatchThreadIndex ].count = 0;
}

struct SHitInfo
{
    float t;
    float u;
    float v;
    uint triangleId;
    uint instanceIndex;
    bool backface;
};

bool BVHIntersectNoInterp( float3 origin
    , float3 direction
    , float tMin
    , uint dispatchThreadIndex
    , StructuredBuffer<Vertex> vertices
    , StructuredBuffer<uint> triangles
    , StructuredBuffer<BVHNode> BVHNodes
    , StructuredBuffer<float4x3> Instances
    , inout SHitInfo hitInfo
    , out uint iterationCounter )
{
    float tMax = FLT_INF;
    float t, u, v;
    bool backface;
    iterationCounter = 0;

    BVHTraversalStackReset( dispatchThreadIndex );

    uint nodeIndex = 0;
    uint instanceIndex = 0;
    bool isBLAS = false;
    float3 localRayOrigin = origin;
    float3 localRayDirection = direction;
    while ( true )
    {
        ++iterationCounter;
        bool popNode = false;
        if ( RayAABBIntersect( localRayOrigin, 1.f / localRayDirection, tMin, tMax, BVHNodes[ nodeIndex ].bboxMin, BVHNodes[ nodeIndex ].bboxMax ) )
        {
            bool hasBLAS = BVHNodeHasBLAS( BVHNodes[ nodeIndex ] );
            uint primCountOrInstanceIndex = BVHNodeGetPrimitiveCount( BVHNodes[ nodeIndex ] );
            if ( hasBLAS )
            {
                // Going in from TLAS to BLAS
                float4x3 instanceInvTransform = Instances[ primCountOrInstanceIndex ];
                localRayOrigin = mul( float4( origin, 1.f ), instanceInvTransform );
                localRayDirection = mul( float4( direction, 0.f ), instanceInvTransform );
                isBLAS = true;
                instanceIndex = primCountOrInstanceIndex;
                nodeIndex = BVHNodes[ nodeIndex ].rightChildOrPrimIndex;
            }
            else
            {
                if ( primCountOrInstanceIndex == 0 )
                {
                    uint splitAxis = BVHNodeGetSplitAxis( BVHNodes[ nodeIndex ] );
#if defined( BVH_NO_FRONT_TO_BACK_TRAVERSAL )
                    bool isDirectionNegative = false;
#else 
                    bool isDirectionNegative = splitAxis == 0 ? localRayDirection.x < 0.f : ( splitAxis == 1 ? localRayDirection.y < 0.f : localRayDirection.z < 0.f );
#endif
                    uint pushNodeIndex = isDirectionNegative ? nodeIndex + 1 : BVHNodes[ nodeIndex ].rightChildOrPrimIndex;
                    nodeIndex = isDirectionNegative ? BVHNodes[ nodeIndex ].rightChildOrPrimIndex : nodeIndex + 1;
                    BVHTraversalStackPushback( dispatchThreadIndex, pushNodeIndex, isBLAS );
                }
                else
                {
                    uint primBegin = BVHNodes[ nodeIndex ].rightChildOrPrimIndex;
                    uint primEnd = primBegin + primCountOrInstanceIndex;
                    for ( uint iPrim = primBegin; iPrim < primEnd; ++iPrim )
                    {
                        float3 v0 = vertices[ triangles[ iPrim * 3 ] ].position;
                        float3 v1 = vertices[ triangles[ iPrim * 3 + 1 ] ].position;
                        float3 v2 = vertices[ triangles[ iPrim * 3 + 2 ] ].position;
                        if ( RayTriangleIntersect( localRayOrigin, localRayDirection, tMin, tMax, v0, v1, v2, t, u, v, backface ) )
                        {
                            tMax = t;
                            hitInfo.t = t;
                            hitInfo.u = u;
                            hitInfo.v = v;
                            hitInfo.backface = backface;
                            hitInfo.triangleId = iPrim;
                            hitInfo.instanceIndex = instanceIndex;
                        }
                    }
                    popNode = true;
                }
            }
        }
        else
        {
            popNode = true;
        }
        if ( popNode )
        {
            bool lastNodeIsBLAS = isBLAS;
            if ( BVHTraversalStackPopback( dispatchThreadIndex, nodeIndex, isBLAS ) )
            {
                // If last node is BLAS and the next one is not, then we are going to pop back to TLAS.
                // The situation when last node is NOT BLAS and the next one is should never happen, there is no way to pop back from a TLAS to BLAS.
                if ( lastNodeIsBLAS != isBLAS )
                {
                    localRayOrigin = origin;
                    localRayDirection = direction;
                }
            }
            else
            {
                break;
            }
        }
    }

    return !isinf( tMax );
}

bool BVHIntersect( float3 origin
    , float3 direction
    , float tMin
    , float tMax
    , uint dispatchThreadIndex
    , StructuredBuffer<Vertex> vertices
    , StructuredBuffer<uint> triangles
    , StructuredBuffer<BVHNode> BVHNodes
    , StructuredBuffer<float4x3> Instances )
{
    BVHTraversalStackReset( dispatchThreadIndex );

    uint nodeIndex = 0;
    bool isBLAS = false;
    float3 localRayOrigin = origin;
    float3 localRayDirection = direction;
    while ( true )
    {
        bool popNode = false;
        if ( RayAABBIntersect( localRayOrigin, 1.f / localRayDirection, tMin, tMax, BVHNodes[ nodeIndex ].bboxMin, BVHNodes[ nodeIndex ].bboxMax ) )
        {
            bool hasBLAS = BVHNodeHasBLAS( BVHNodes[ nodeIndex ] );
            uint primCountOrInstanceIndex = BVHNodeGetPrimitiveCount( BVHNodes[ nodeIndex ] );
            if ( hasBLAS )
            {
                // Going in from TLAS to BLAS
                float4x3 instanceInvTransform = Instances[ primCountOrInstanceIndex ];
                localRayOrigin = mul( float4( origin, 1.f ), instanceInvTransform );
                localRayDirection = mul( float4( direction, 0.f ), instanceInvTransform );
                isBLAS = true;
                nodeIndex = BVHNodes[ nodeIndex ].rightChildOrPrimIndex;
            }
            else
            {
                if ( primCountOrInstanceIndex == 0 )
                {
                    uint splitAxis = BVHNodeGetSplitAxis( BVHNodes[ nodeIndex ] );
#if defined( BVH_NO_FRONT_TO_BACK_TRAVERSAL )
                    bool isDirectionNegative = false;
#else 
                    bool isDirectionNegative = splitAxis == 0 ? localRayDirection.x < 0.f : ( splitAxis == 1 ? localRayDirection.y < 0.f : localRayDirection.z < 0.f );
#endif
                    uint pushNodeIndex = isDirectionNegative ? nodeIndex + 1 : BVHNodes[ nodeIndex ].rightChildOrPrimIndex;
                    nodeIndex = isDirectionNegative ? BVHNodes[ nodeIndex ].rightChildOrPrimIndex : nodeIndex + 1;
                    BVHTraversalStackPushback( dispatchThreadIndex, pushNodeIndex, isBLAS );
                }
                else
                {
                    uint primBegin = BVHNodes[ nodeIndex ].rightChildOrPrimIndex;
                    uint primEnd = primBegin + primCountOrInstanceIndex;
                    for ( uint iPrim = primBegin; iPrim < primEnd; ++iPrim )
                    {
                        float3 v0 = vertices[ triangles[ iPrim * 3 ] ].position;
                        float3 v1 = vertices[ triangles[ iPrim * 3 + 1 ] ].position;
                        float3 v2 = vertices[ triangles[ iPrim * 3 + 2 ] ].position;
                        float t, u, v;
                        bool backface;
                        if ( RayTriangleIntersect( localRayOrigin, localRayDirection, tMin, tMax, v0, v1, v2, t, u, v, backface ) )
                        {
                            return true;
                        }
                    }
                    popNode = true;
                }
            }
        }
        else
        {
            popNode = true;
        }
        if ( popNode )
        {
            bool lastNodeIsBLAS = isBLAS;
            if ( BVHTraversalStackPopback( dispatchThreadIndex, nodeIndex, isBLAS ) )
            {
                // If last node is BLAS and the next one is not, then we are going to pop back to TLAS.
                // The situation when last node is NOT BLAS and the next one IS should never happen, there is no way to pop back from a TLAS to BLAS.
                if ( lastNodeIsBLAS != isBLAS )
                {
                    localRayOrigin = origin;
                    localRayDirection = direction;
                }
            }
            else
            {
                break;
            }
        }
    }

    return false;
}

#endif