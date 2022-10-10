#include "stdafx.h"
#include "BVHAccel.h"
#include "../Shaders/Vertex.inc.hlsl"
#include "../Shaders/BVHNode.inc.hlsl"

static void CalculateTriangleBoundingBox( const GPU::Vertex& vert0, const GPU::Vertex& vert1, const GPU::Vertex& vert2, DirectX::BoundingBox* m_BoundingBox )
{
    DirectX::XMVECTOR v0 = DirectX::XMLoadFloat3( &vert0.position );
    DirectX::XMVECTOR v1 = DirectX::XMLoadFloat3( &vert1.position );
    DirectX::XMVECTOR v2 = DirectX::XMLoadFloat3( &vert2.position );
    DirectX::XMVECTOR vMin = DirectX::XMVectorMin( v2, DirectX::XMVectorMin( v0, v1 ) ); // Min
    DirectX::XMVECTOR vMax = DirectX::XMVectorMax( v2, DirectX::XMVectorMax( v0, v1 ) ); // Max
    DirectX::BoundingBox::CreateFromPoints( *m_BoundingBox, vMin, vMax );
}

static void TransformedBoundingBox( const DirectX::BoundingBox& originalBBox, const DirectX::XMFLOAT4X3& transform, DirectX::BoundingBox* m_BoundingBox )
{
    DirectX::XMMATRIX vTransform = DirectX::XMLoadFloat4x3( &transform );
    originalBBox.Transform( *m_BoundingBox, vTransform );
}

float BoundingBoxSurfaceArea( const DirectX::BoundingBox& m_BoundingBox )
{
    return 4.f * ( m_BoundingBox.Extents.x * m_BoundingBox.Extents.y + m_BoundingBox.Extents.x * m_BoundingBox.Extents.z + m_BoundingBox.Extents.y * m_BoundingBox.Extents.z );
}

struct SPrimitiveInfo
{
    uint32_t m_PrimIndex;
    DirectX::BoundingBox m_BoundingBox;

    struct BBoxCenterPerAxisLessPred
    {
        BBoxCenterPerAxisLessPred( int axis ) : axis( axis ) {}
        bool operator()( const SPrimitiveInfo& a, const SPrimitiveInfo& b ) const
        {
            return ( (float*) ( &a.m_BoundingBox.Center ) )[ axis ] < ( (float*) ( &b.m_BoundingBox.Center ) )[ axis ];
        }
        int axis;
    };
};

struct BucketLessEqualPred
{
    BucketLessEqualPred( uint32_t split, uint32_t nBuckets, uint32_t axis, const DirectX::BoundingBox& b )
        : centroidBox( b ), splitBucket( split ), nBuckets( nBuckets ), axis( axis ) { }

    bool operator()( const SPrimitiveInfo& p ) const
    {
        float min = ( (float*) &centroidBox.Center )[ axis ] - ( (float*) &centroidBox.Extents )[ axis ];
        float size = ( (float*) &centroidBox.Extents )[ axis ] * 2.0f;
        uint32_t b = uint32_t( nBuckets * ( ( ( (float*) &p.m_BoundingBox.Center )[ axis ] ) - min ) / size );
        if ( b == nBuckets ) b = nBuckets - 1;
        return b <= splitBucket;
    }

    uint32_t splitBucket, nBuckets, axis;
    const DirectX::BoundingBox& centroidBox;
};

struct TriangleIndices
{
    uint32_t indices[ 3 ];
};

struct BVHNodeInfo
{
    int parentIndex;
    uint32_t primBegin;
    uint32_t primEnd;
    uint32_t depth;
};

template<typename PrimitiveType, bool HasPrimitive, bool HasLeafNodeDepths>
static void BuildNodes( 
      std::vector<SPrimitiveInfo>& primitiveInfos
    , const PrimitiveType* primitives
    , const BVHNodeInfo& rootNodeInfo
    , uint32_t maxPrimitiveCountInNode
    , PrimitiveType* reorderedPrimitives
    , uint32_t* reorderedPrimitiveIndices
    , uint32_t& reorderedPrimitiveCount
    , std::vector<BVHAccel::BVHNode>* BVHNodes
    , uint32_t* maxDepth
    , uint32_t* maxStackSize
    , uint32_t* leafNodeDepths )
{
    using BVHNode = BVHAccel::BVHNode;

    std::stack<BVHNodeInfo> stack;

    BVHNodeInfo currentNodeInfo = rootNodeInfo;
    while ( true )
    {
        assert( currentNodeInfo.primEnd != currentNodeInfo.primBegin );

        uint32_t BVHNodeIndex = (uint32_t)BVHNodes->size();

        if ( currentNodeInfo.parentIndex != -1 )
        {
            ( *BVHNodes )[ currentNodeInfo.parentIndex ].m_ChildIndex = BVHNodeIndex;
        }
        
        assert( BVHNodes->size() < UINT_MAX );
        BVHNodes->emplace_back();
        BVHNode* BVHNode = &BVHNodes->back();

        BVHNode->m_PrimCount = 0;
        BVHNode->m_IsLeaf = false;

        // Calculate bounding box for this node
        BVHNode->m_BoundingBox = primitiveInfos[ currentNodeInfo.primBegin ].m_BoundingBox;
        for ( uint32_t iPrim = currentNodeInfo.primBegin + 1; iPrim < currentNodeInfo.primEnd; ++iPrim )
        {
            DirectX::BoundingBox::CreateMerged( BVHNode->m_BoundingBox, BVHNode->m_BoundingBox, primitiveInfos[ iPrim ].m_BoundingBox );
        }

        uint32_t m_PrimCount = currentNodeInfo.primEnd - currentNodeInfo.primBegin;
        if ( m_PrimCount == 1 )
        {
            uint32_t m_PrimIndex = primitiveInfos[ currentNodeInfo.primBegin ].m_PrimIndex;
            if ( HasPrimitive )
            {
                reorderedPrimitives[ reorderedPrimitiveCount ] = primitives[ m_PrimIndex ];
            }
            reorderedPrimitiveIndices[ reorderedPrimitiveCount ] = m_PrimIndex;
            BVHNode->m_PrimIndex = reorderedPrimitiveCount;
            BVHNode->m_PrimCount = 1;
            BVHNode->m_IsLeaf = true;
            reorderedPrimitiveCount += 1;
            if ( HasLeafNodeDepths )
            {
                leafNodeDepths[ BVHNode->m_PrimIndex ] = currentNodeInfo.depth;
            }
            
            if ( !stack.empty() )
            {
                currentNodeInfo = stack.top();
                stack.pop();
                continue;
            }
            else
            {
                break;
            }
        }
        else
        {
            // Compute centroid bounding box
            DirectX::BoundingBox centroidBox;
            {
                DirectX::XMVECTOR vCentroidMin, vCentroidMax, vCentroid;
                vCentroidMin = DirectX::XMLoadFloat3( &primitiveInfos[ currentNodeInfo.primBegin ].m_BoundingBox.Center );
                vCentroidMax = vCentroidMin;
                for ( size_t i = currentNodeInfo.primBegin + 1; i < currentNodeInfo.primEnd; ++i )
                {
                    vCentroid = DirectX::XMLoadFloat3( &primitiveInfos[ i ].m_BoundingBox.Center );
                    vCentroidMax = DirectX::XMVectorMax( vCentroidMax, vCentroid );
                    vCentroidMin = DirectX::XMVectorMin( vCentroidMin, vCentroid );
                }
                DirectX::BoundingBox::CreateFromPoints( centroidBox, vCentroidMin, vCentroidMax );
            }

            // Find the axis with the max extend
            int axis = 0;
            {
                float max = centroidBox.Extents.x;
                if ( centroidBox.Extents.y > max )
                {
                    max = centroidBox.Extents.y;
                    axis = 1;
                }
                if ( centroidBox.Extents.z > max )
                    axis = 2;
            }

            BVHNode->m_SplitAxis = axis;

            // Partition
            uint32_t primMiddle = ( currentNodeInfo.primBegin + currentNodeInfo.primEnd ) / 2;

            // Handle special occasion
            if ( ( (float*)( &centroidBox.Extents ) )[ axis ] == 0.f )
            {
                if ( m_PrimCount < maxPrimitiveCountInNode )
                {
                    for ( size_t iPrim = 0; iPrim < m_PrimCount; ++iPrim )
                    {
                        uint32_t m_PrimIndex = primitiveInfos[ currentNodeInfo.primBegin + iPrim ].m_PrimIndex;
                        if ( HasPrimitive )
                        {
                            reorderedPrimitives[ reorderedPrimitiveCount + iPrim ] = primitives[ m_PrimIndex ];
                        }
                        reorderedPrimitiveIndices[ reorderedPrimitiveCount + iPrim ] = m_PrimIndex;
                    }
                    BVHNode->m_PrimIndex = reorderedPrimitiveCount;
                    BVHNode->m_PrimCount = uint8_t( m_PrimCount );
                    BVHNode->m_IsLeaf = true;
                    reorderedPrimitiveCount += m_PrimCount;
                    if ( HasLeafNodeDepths )
                    {
                        leafNodeDepths[ BVHNode->m_PrimIndex ] = currentNodeInfo.depth;
                    }

                    if ( !stack.empty() )
                    {
                        currentNodeInfo = stack.top();
                        stack.pop();
                        continue;
                    }
                    else
                    {
                        break;
                    }
                }
                else
                { 
                    currentNodeInfo.depth++;
                    stack.push( { (int)BVHNodeIndex, primMiddle, currentNodeInfo.primEnd, currentNodeInfo.depth } );
                    currentNodeInfo.parentIndex = -1;
                    currentNodeInfo.primEnd = primMiddle;
                    *maxDepth = std::max( *maxDepth, currentNodeInfo.depth );
                    *maxStackSize = std::max( *maxStackSize, (uint32_t)stack.size() );
                    continue;
                }
            }

            if ( m_PrimCount <= 4 )
            {
                // If primitives number is smaller than 4, devide equally
                std::nth_element( &primitiveInfos[ currentNodeInfo.primBegin ], &primitiveInfos[ primMiddle ], &primitiveInfos[ currentNodeInfo.primEnd - 1 ] + 1, SPrimitiveInfo::BBoxCenterPerAxisLessPred( axis ) );
            }
            else
            {
                const size_t k_BucketsCount = 12;
                struct Bucket
                {
                    uint32_t                m_PrimCount;
                    DirectX::BoundingBox    m_BoundingBox;
                    Bucket() : m_PrimCount( 0 ) { }
                };
                Bucket buckets[ k_BucketsCount ];
                for ( size_t i = currentNodeInfo.primBegin; i < currentNodeInfo.primEnd; ++i )
                {
                    float min = ( (float*)&centroidBox.Center )[ axis ] - ( (float*)&centroidBox.Extents )[ axis ];
                    float size = ( (float*)&centroidBox.Extents )[ axis ] * 2.0f;
                    uint32_t n = uint32_t( k_BucketsCount * ( ( ( (float*)&primitiveInfos[ i ].m_BoundingBox.Center )[ axis ] ) - min ) / size );
                    if ( n == k_BucketsCount ) --n;
                    if ( buckets[ n ].m_PrimCount == 0 )
                    {
                        buckets[ n ].m_BoundingBox = primitiveInfos[ i ].m_BoundingBox;
                    }
                    else
                    {
                        DirectX::BoundingBox::CreateMerged( buckets[ n ].m_BoundingBox, buckets[ n ].m_BoundingBox, primitiveInfos[ i ].m_BoundingBox );
                    }
                    assert( buckets[ n ].m_PrimCount < std::numeric_limits<decltype( Bucket::m_PrimCount )>::max() );
                    buckets[ n ].m_PrimCount++;
                }

                // Compute cost for each splitting
                float cost[ k_BucketsCount - 1 ];
                for ( size_t i = 0; i < k_BucketsCount - 1; ++i )
                {
                    uint32_t count0 = 0, count1 = 0;
                    DirectX::BoundingBox b0 = buckets[ 0 ].m_BoundingBox;
                    for ( size_t j = 1; j <= i; ++j )
                    {
                        DirectX::BoundingBox::CreateMerged( b0, b0, buckets[ j ].m_BoundingBox );
                        count0 += buckets[ j ].m_PrimCount;
                    }

                    DirectX::BoundingBox b1 = buckets[ i + 1 ].m_BoundingBox;
                    for ( size_t j = i + 2; j < k_BucketsCount; ++j )
                    {
                        DirectX::BoundingBox::CreateMerged( b1, b1, buckets[ j ].m_BoundingBox );
                        count1 += buckets[ j ].m_PrimCount;
                    }

                    cost[ i ] = .125f + ( count0 * BoundingBoxSurfaceArea( b0 ) + count1 * BoundingBoxSurfaceArea( b1 ) )
                        / BoundingBoxSurfaceArea( BVHNode->m_BoundingBox );
                }

                // Find smallest cost
                uint32_t minCostIndex = (uint32_t)std::distance( std::begin( cost ), std::min_element( std::begin( cost ), std::end( cost ) ) );
                float minCost = cost[ minCostIndex ];

                if ( m_PrimCount > maxPrimitiveCountInNode || minCost < m_PrimCount )
                {
                    SPrimitiveInfo* prim = std::partition( &primitiveInfos[ currentNodeInfo.primBegin ], &primitiveInfos[ currentNodeInfo.primEnd - 1 ] + 1,
                        BucketLessEqualPred( minCostIndex, k_BucketsCount, axis, centroidBox ) );
                    primMiddle = (uint32_t)std::distance( primitiveInfos.data(), prim );
                }
                else
                {
                    // Create leaf node
                    for ( size_t i = 0; i < m_PrimCount; ++i )
                    {
                        uint32_t m_PrimIndex = primitiveInfos[ currentNodeInfo.primBegin + i ].m_PrimIndex;
                        if ( HasPrimitive )
                        {
                            reorderedPrimitives[ reorderedPrimitiveCount + i ] = primitives[ m_PrimIndex ];
                        }
                        reorderedPrimitiveIndices[ reorderedPrimitiveCount + i ] = m_PrimIndex;
                    }
                    BVHNode->m_PrimIndex = reorderedPrimitiveCount;
                    BVHNode->m_PrimCount = uint8_t( m_PrimCount );
                    BVHNode->m_IsLeaf = true;
                    reorderedPrimitiveCount += m_PrimCount;
                    if ( HasLeafNodeDepths )
                    {
                        leafNodeDepths[ BVHNode->m_PrimIndex ] = currentNodeInfo.depth;
                    }

                    if ( !stack.empty() )
                    {
                        currentNodeInfo = stack.top();
                        stack.pop();
                        continue;
                    }
                    else
                    {
                        break;
                    }
                }
            }
            
            currentNodeInfo.depth++;
            stack.push( { (int)BVHNodeIndex, primMiddle, currentNodeInfo.primEnd, currentNodeInfo.depth } );
            currentNodeInfo.parentIndex = -1;
            currentNodeInfo.primEnd = primMiddle;
            *maxDepth = std::max( *maxDepth, currentNodeInfo.depth );
            *maxStackSize = std::max(*maxStackSize, (uint32_t)stack.size());
        }
    }
}

namespace BVHAccel
{ 

void BuildBLAS( const GPU::Vertex* vertices, const uint32_t* indices, uint32_t* reorderedIndices, uint32_t* reorderedTriangleIndices, uint32_t triangleCount, std::vector<BVHNode>* BVHNodes, uint32_t* maxDepth, uint32_t* maxStackSize )
{
    std::vector<SPrimitiveInfo> primitiveInfos;
    primitiveInfos.reserve( triangleCount );
    for ( uint32_t iPrim = 0; iPrim < triangleCount; ++iPrim )
    {
        uint32_t index0 = indices[ iPrim * 3 ];
        uint32_t index1 = indices[ iPrim * 3 + 1 ];
        uint32_t index2 = indices[ iPrim * 3 + 2 ];
        primitiveInfos.emplace_back();
        SPrimitiveInfo& newBVHPrim = primitiveInfos.back();
        CalculateTriangleBoundingBox( vertices[ index0 ], vertices[ index1 ], vertices[ index2 ], &newBVHPrim.m_BoundingBox );
        newBVHPrim.m_PrimIndex = iPrim;
    }

    uint32_t reorderedTriangleCount = 0;
    BuildNodes<TriangleIndices, true, false>( primitiveInfos, (TriangleIndices*)indices, { -1, 0, triangleCount, 0 }, 2, (TriangleIndices*)reorderedIndices, reorderedTriangleIndices, reorderedTriangleCount, BVHNodes, maxDepth, maxStackSize, nullptr );
    assert( reorderedTriangleCount == triangleCount );
}

void BuildTLAS( const SInstance* instances, uint32_t* reorderedInstanceIndices, uint32_t instanceCount, std::vector<BVHNode>* BVHNodes, uint32_t* maxDepth, uint32_t* maxStackSize, uint32_t* instanceDepths )
{
    std::vector<SPrimitiveInfo> primitiveInfos;
    primitiveInfos.reserve( instanceCount );
    for ( uint32_t iPrim = 0; iPrim < instanceCount; ++iPrim )
    {
        primitiveInfos.emplace_back();
        SPrimitiveInfo& newBVHPrim = primitiveInfos.back();
        TransformedBoundingBox( instances[ iPrim ].m_BoundingBox, instances[ iPrim ].m_Transform, &newBVHPrim.m_BoundingBox );
        newBVHPrim.m_PrimIndex = iPrim;
    }

    uint32_t reorderedInstanceCount = 0;
    BuildNodes<int, false, true>( primitiveInfos, nullptr, { -1, 0, instanceCount, 0 }, 1, nullptr, reorderedInstanceIndices, reorderedInstanceCount, BVHNodes, maxDepth, maxStackSize, instanceDepths );
    assert( reorderedInstanceCount == instanceCount );
}

void PackBVH( const BVHNode* BVHNodes, uint32_t nodeCount, bool isBLAS, GPU::BVHNode* packedBVHNodes, uint32_t nodeIndexOffset, uint32_t primitiveIndexOffset )
{
    if ( nodeCount > 0 )
    {
        for ( uint32_t iNode = 0; iNode < nodeCount; ++iNode )
        {
            const BVHNode& unpacked = BVHNodes[ iNode ];
            GPU::BVHNode& packed = packedBVHNodes[ iNode ];

            DirectX::XMVECTOR vCenter = DirectX::XMLoadFloat3( &unpacked.m_BoundingBox.Center );
            DirectX::XMVECTOR vExtends = DirectX::XMLoadFloat3( &unpacked.m_BoundingBox.Extents );
            DirectX::XMVECTOR vBBoxMin = DirectX::XMVectorSubtract( vCenter, vExtends );
            DirectX::XMVECTOR vBBoxMax = DirectX::XMVectorAdd( vCenter, vExtends );
            DirectX::XMStoreFloat3( &packed.bboxMin, vBBoxMin );
            DirectX::XMStoreFloat3( &packed.bboxMax, vBBoxMax );

            packed.rightChildOrPrimIndex = unpacked.m_ChildIndex;
            packed.misc = unpacked.m_PrimCount;
            packed.misc |= ( unpacked.m_SplitAxis & 0x3 ) << 9;
            if ( !unpacked.m_IsLeaf )
            {
                packed.rightChildOrPrimIndex += nodeIndexOffset;
            }
            else if ( isBLAS )
            {
                packed.rightChildOrPrimIndex += primitiveIndexOffset;
            }
            if ( !isBLAS && unpacked.m_IsLeaf )
            {
                packed.misc |= 0x100;
            }
        }
    }
}

void SerializeBVHToXML( const BVHNode* rootNode, FILE* file )
{
    struct TraversalNode
    {
        const BVHNode* node;
        bool isLeftChild;
        uint32_t nodeIndex;
    };

    std::stack<TraversalNode> stack;

    fprintf( file, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n" );

    TraversalNode currentNode = { rootNode, false, 0 };
    while ( true )
    {
        if ( currentNode.node->m_PrimCount != 0 )
        {
            fprintf( file, "<Node Index=\"%d\" Center=\"%.4f,%.4f,%.4f\" Extents=\"%.4f,%.4f,%.4f\" PrimIndex=\"%d\" PrimCount=\"%d\"/>\n",
                currentNode.nodeIndex,
                currentNode.node->m_BoundingBox.Center.x,
                currentNode.node->m_BoundingBox.Center.y,
                currentNode.node->m_BoundingBox.Center.z,
                currentNode.node->m_BoundingBox.Extents.x,
                currentNode.node->m_BoundingBox.Extents.y,
                currentNode.node->m_BoundingBox.Extents.z,
                currentNode.node->m_PrimIndex,
                currentNode.node->m_PrimCount );

            if ( currentNode.isLeftChild )
            {
                currentNode = { &rootNode[ stack.top().node->m_ChildIndex ], false, stack.top().node->m_ChildIndex };
                continue;
            }
            else
            {
                if ( stack.empty() )
                    break;

                do
                {
                    currentNode = stack.top();
                    stack.pop();
                    fprintf( file, "</Node>\n" );
                } while ( !currentNode.isLeftChild && !stack.empty() );

                if ( !stack.empty() )
                {
                    currentNode = { &rootNode[ stack.top().node->m_ChildIndex ], false, stack.top().node->m_ChildIndex };
                    continue;
                }
                else
                {
                    break;
                }
            }
        }
        else
        {
            fprintf( file, "<Node Index=\"%d\" Center=\"%.4f,%.4f,%.4f\" Extents=\"%.4f,%.4f,%.4f\" ChildIndex=\"%d\" SplitAxis=\"%d\">\n",
                currentNode.nodeIndex,
                currentNode.node->m_BoundingBox.Center.x,
                currentNode.node->m_BoundingBox.Center.y,
                currentNode.node->m_BoundingBox.Center.z,
                currentNode.node->m_BoundingBox.Extents.x,
                currentNode.node->m_BoundingBox.Extents.y,
                currentNode.node->m_BoundingBox.Extents.z,
                currentNode.node->m_ChildIndex,
                currentNode.node->m_SplitAxis );

            stack.push( currentNode );
            currentNode = { currentNode.node + 1, true, currentNode.nodeIndex + 1 };
        }
    }
}

}
