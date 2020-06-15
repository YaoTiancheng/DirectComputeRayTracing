#ifndef _BVHACCEL_H_
#define _BVHACCEL_H_

#include "Primitives.inc.hlsl"

struct BVHNode
{
    float3 bboxMin;
    float3 bboxMax;
    uint   childOrPrimIndex;
    uint   misc;
};

uint BVHNodeGetPrimitiveCount( BVHNode node )
{
    return ( node.misc & 0xFF );
}

struct BVHTraversalStack
{
    uint nodeIndex[ 8 ];
    int count;
};

groupshared BVHTraversalStack gs_BVHTraversalStacks[ 256 ]; // Should match group size

bool BVHTraversalStackPopback( uint dispatchThreadIndex, out uint nodeIndex )
{
    bool isEmpty = ( gs_BVHTraversalStacks[ dispatchThreadIndex ].count == 0 );
    nodeIndex = isEmpty ? 0 : gs_BVHTraversalStacks[ dispatchThreadIndex ].nodeIndex[ --gs_BVHTraversalStacks[ dispatchThreadIndex ].count ];
    return !isEmpty;
}

void BVHTraversalStackPushback( uint dispatchThreadIndex, uint index )
{
    gs_BVHTraversalStacks[ dispatchThreadIndex ].nodeIndex[ gs_BVHTraversalStacks[ dispatchThreadIndex ].count++ ] = index;
}

void BVHTraversalStackReset( uint dispatchThreadIndex )
{
    gs_BVHTraversalStacks[ dispatchThreadIndex ].count = 0;
}


StructuredBuffer<BVHNode> g_BVHNodes : register( t10 );

bool BVHIntersect( float3 origin
    , float3 direction
    , float tMin
    , uint dispatchThreadIndex
    , out float t
    , out Intersection intersection )
{
    float tMax = 1.0f / 0.0f;
    float3 invDir = 1.0f / direction;

    BVHTraversalStackReset( dispatchThreadIndex );

    uint nodeIndex = 0;
    while ( true )
    {
        if ( RayAABBIntersect( origin, invDir, tMin, tMax, g_BVHNodes[ nodeIndex ].bboxMin, g_BVHNodes[ nodeIndex ].bboxMax ) )
        {
            uint primCount = BVHNodeGetPrimitiveCount( g_BVHNodes[ nodeIndex ] );
            if ( primCount == 0 )
            {
                BVHTraversalStackPushback( dispatchThreadIndex, g_BVHNodes[ nodeIndex ].childOrPrimIndex );
                nodeIndex++;
            }
            else
            {
                uint primBegin = g_BVHNodes[ nodeIndex ].childOrPrimIndex;
                uint primEnd = primBegin + primCount;
                for ( uint iPrim = primBegin; iPrim < primEnd; ++iPrim )
                {
                    Vertex v0 = g_Vertices[ g_Triangles[ iPrim * 3 ] ];
                    Vertex v1 = g_Vertices[ g_Triangles[ iPrim * 3 + 1 ] ];
                    Vertex v2 = g_Vertices[ g_Triangles[ iPrim * 3 + 2 ] ];
                    if ( RayTriangleIntersect( origin, direction, tMin, tMax, v0, v1, v2, t, intersection ) )
                    {
                        tMax = t;
                    }
                }
                if ( !BVHTraversalStackPopback( dispatchThreadIndex, nodeIndex ) )
                    break;
            }
        }
        else
        {
            if ( !BVHTraversalStackPopback( dispatchThreadIndex, nodeIndex ) )
                break;
        }
    }

    return !isinf( tMax );
}

bool BVHIntersect( float3 origin
    , float3 direction
    , float tMin
    , uint dispatchThreadIndex )
{
    float tMax = 1.0f / 0.0f;
    float3 invDir = 1.0f / direction;
    Intersection intersection;

    BVHTraversalStackReset( dispatchThreadIndex );

    uint nodeIndex = 0;
    while ( true )
    {
        if ( RayAABBIntersect( origin, invDir, tMin, tMax, g_BVHNodes[ nodeIndex ].bboxMin, g_BVHNodes[ nodeIndex ].bboxMax ) )
        {
            uint primCount = BVHNodeGetPrimitiveCount( g_BVHNodes[ nodeIndex ] );
            if ( primCount == 0 )
            {
                BVHTraversalStackPushback( dispatchThreadIndex, g_BVHNodes[ nodeIndex ].childOrPrimIndex );
                nodeIndex++;
            }
            else
            {
                uint primBegin = g_BVHNodes[ nodeIndex ].childOrPrimIndex;
                uint primEnd = primBegin + primCount;
                for ( uint iPrim = primBegin; iPrim < primEnd; ++iPrim )
                {
                    Vertex v0 = g_Vertices[ g_Triangles[ iPrim * 3 ] ];
                    Vertex v1 = g_Vertices[ g_Triangles[ iPrim * 3 + 1 ] ];
                    Vertex v2 = g_Vertices[ g_Triangles[ iPrim * 3 + 2 ] ];
                    float t, u, v;
                    if ( RayTriangleIntersect( origin, direction, tMin, tMax, v0, v1, v2, t, u, v ) )
                    {
                        return true;
                    }
                }
                if ( !BVHTraversalStackPopback( dispatchThreadIndex, nodeIndex ) )
                    break;
            }
        }
        else
        {
            if ( !BVHTraversalStackPopback( dispatchThreadIndex, nodeIndex ) )
                break;
        }
    }

    return false;
}

#endif