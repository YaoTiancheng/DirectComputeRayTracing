#ifndef _BVHACCEL_H_
#define _BVHACCEL_H_

#include "RayPrimitiveIntersect.inc.hlsl"

uint BVHNodeGetPrimitiveCount( BVHNode node )
{
    return ( node.misc & 0xFF );
}

struct BVHTraversalStack
{
    uint nodeIndex[ RT_BVH_TRAVERSAL_STACK_SIZE ];
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

struct SHitInfo
{
    float t;
    float u;
    float v;
    uint triangleId;
    bool backface;
};

bool BVHIntersectNoInterp( float3 origin
    , float3 direction
    , float tMin
    , uint dispatchThreadIndex
    , StructuredBuffer<Vertex> vertices
    , StructuredBuffer<uint> triangles
    , StructuredBuffer<BVHNode> BVHNodes
    , uint primitiveCount
    , inout SHitInfo hitInfo )
{
    float tMax = FLT_INF;
    float t, u, v;
    bool backface;

#if !defined( NO_BVH_ACCEL )
    float3 invDir = 1.0f / direction;

    BVHTraversalStackReset( dispatchThreadIndex );

    uint nodeIndex = 0;
    while ( true )
    {
        if ( RayAABBIntersect( origin, invDir, tMin, tMax, BVHNodes[ nodeIndex ].bboxMin, BVHNodes[ nodeIndex ].bboxMax ) )
        {
            uint primCount = BVHNodeGetPrimitiveCount( BVHNodes[ nodeIndex ] );
            if ( primCount == 0 )
            {
                BVHTraversalStackPushback( dispatchThreadIndex, BVHNodes[ nodeIndex ].childOrPrimIndex );
                nodeIndex++;
            }
            else
            {
                uint primBegin = BVHNodes[ nodeIndex ].childOrPrimIndex;
                uint primEnd = primBegin + primCount;
                for ( uint iPrim = primBegin; iPrim < primEnd; ++iPrim )
                {
                    Vertex v0 = vertices[ triangles[ iPrim * 3 ] ];
                    Vertex v1 = vertices[ triangles[ iPrim * 3 + 1 ] ];
                    Vertex v2 = vertices[ triangles[ iPrim * 3 + 2 ] ];
                    if ( RayTriangleIntersect( origin, direction, tMin, tMax, v0, v1, v2, t, u, v, backface ) )
                    {
                        tMax = t;
                        hitInfo.t = t;
                        hitInfo.u = u;
                        hitInfo.v = v;
                        hitInfo.backface = backface;
                        hitInfo.triangleId = iPrim;
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
#else
    for ( uint iPrim = 0; iPrim < primitiveCount; ++iPrim )
    {
        Vertex v0 = vertices[ triangles[ iPrim * 3 ] ];
        Vertex v1 = vertices[ triangles[ iPrim * 3 + 1 ] ];
        Vertex v2 = vertices[ triangles[ iPrim * 3 + 2 ] ];
        if ( RayTriangleIntersect( origin, direction, tMin, tMax, v0, v1, v2, t, u, v, backface ) )
        {
            tMax = t;
            hitInfo.t = t;
            hitInfo.u = u;
            hitInfo.v = v;
            hitInfo.backface = backface;
            hitInfo.triangleId = iPrim;
        }
    }
#endif

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
    , uint primitiveCount )
{
#if !defined( NO_BVH_ACCEL )
    float3 invDir = 1.0f / direction;

    BVHTraversalStackReset( dispatchThreadIndex );

    uint nodeIndex = 0;
    while ( true )
    {
        if ( RayAABBIntersect( origin, invDir, tMin, tMax, BVHNodes[ nodeIndex ].bboxMin, BVHNodes[ nodeIndex ].bboxMax ) )
        {
            uint primCount = BVHNodeGetPrimitiveCount( BVHNodes[ nodeIndex ] );
            if ( primCount == 0 )
            {
                BVHTraversalStackPushback( dispatchThreadIndex, BVHNodes[ nodeIndex ].childOrPrimIndex );
                nodeIndex++;
            }
            else
            {
                uint primBegin = BVHNodes[ nodeIndex ].childOrPrimIndex;
                uint primEnd = primBegin + primCount;
                for ( uint iPrim = primBegin; iPrim < primEnd; ++iPrim )
                {
                    Vertex v0 = vertices[ triangles[ iPrim * 3 ] ];
                    Vertex v1 = vertices[ triangles[ iPrim * 3 + 1 ] ];
                    Vertex v2 = vertices[ triangles[ iPrim * 3 + 2 ] ];
                    float t, u, v;
                    bool backface;
                    if ( RayTriangleIntersect( origin, direction, tMin, tMax, v0, v1, v2, t, u, v, backface ) )
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
#else
    float t, u, v;
    bool backface;
    for ( uint iPrim = 0; iPrim < primitiveCount; ++iPrim )
    {
        Vertex v0 = vertices[ triangles[ iPrim * 3 ] ];
        Vertex v1 = vertices[ triangles[ iPrim * 3 + 1 ] ];
        Vertex v2 = vertices[ triangles[ iPrim * 3 + 2 ] ];
        if ( RayTriangleIntersect( origin, direction, tMin, tMax, v0, v1, v2, t, u, v, backface ) )
        {
            return true;
        }
    }
#endif

    return false;
}

#endif