#include "stdafx.h"
#include "BVHAccel.h"
#include "Primitive.h"

static const uint8_t k_MaxPrimCountInNode = 2;

static void CalculateTriangleBoundingBox( const Vertex& vert0, const Vertex& vert1, const Vertex& vert2, DirectX::BoundingBox* bbox )
{
    DirectX::XMVECTOR v0 = DirectX::XMLoadFloat4( &vert0.position );
    DirectX::XMVECTOR v1 = DirectX::XMLoadFloat4( &vert1.position );
    DirectX::XMVECTOR v2 = DirectX::XMLoadFloat4( &vert2.position );
    DirectX::XMVECTOR vMin = DirectX::XMVectorMin( v2, DirectX::XMVectorMin( v0, v1 ) ); // Min
    DirectX::XMVECTOR vMax = DirectX::XMVectorMax( v2, DirectX::XMVectorMax( v0, v1 ) ); // Max
    DirectX::BoundingBox::CreateFromPoints( *bbox, vMin, vMax );
}

float BoundingBoxSurfaceArea( const DirectX::BoundingBox& bbox )
{
    return 4.f * ( bbox.Extents.x * bbox.Extents.y + bbox.Extents.x * bbox.Extents.z + bbox.Extents.y * bbox.Extents.z );
}

struct PrimitiveInfo
{
    uint32_t                primIndex;
    DirectX::BoundingBox    bbox;

    struct BBoxCenterPerAxisLessPred
    {
        BBoxCenterPerAxisLessPred( int axis ) : axis( axis ) {}
        bool operator()( const PrimitiveInfo& a, const PrimitiveInfo& b ) const
        {
            return ( (float*) ( &a.bbox.Center ) )[ axis ] < ( (float*) ( &b.bbox.Center ) )[ axis ];
        }
        int axis;
    };
};

struct BucketLessEqualPred
{
    BucketLessEqualPred( uint32_t split, uint32_t nBuckets, uint32_t axis, const DirectX::BoundingBox& b )
        : centroidBox( b ), splitBucket( split ), nBuckets( nBuckets ), axis( axis ) { }

    bool operator()( const PrimitiveInfo& p ) const
    {
        float min = ( (float*) &centroidBox.Center )[ axis ] - ( (float*) &centroidBox.Extents )[ axis ];
        float size = ( (float*) &centroidBox.Extents )[ axis ] * 2.0f;
        uint32_t b = uint32_t( nBuckets * ( ( ( (float*) &p.bbox.Center )[ axis ] ) - min ) / size );
        if ( b == nBuckets ) b = nBuckets - 1;
        return b <= splitBucket;
    }

    uint32_t splitBucket, nBuckets, axis;
    const DirectX::BoundingBox& centroidBox;
};

struct TriangleIndices
{
    uint32_t        indices[ 3 ];
};

static void BuildNodeRecursively( 
      std::vector<PrimitiveInfo>& primitiveInfos
    , const Vertex* vertices
    , const TriangleIndices* triangles
    , uint32_t primBegin
    , uint32_t primEnd
    , TriangleIndices* reorderedTriangles
    , uint32_t& reorderedTrianglesCount
    , std::vector<UnpackedBVHNode>* bvhNodes
)
{
    assert( primEnd != primBegin );

    uint32_t bvhNodeIndex = bvhNodes->size();
    bvhNodes->emplace_back();
    UnpackedBVHNode* bvhNode = &bvhNodes->back();

    bvhNode->primCount = 0;

    // Calculate bounding box for this node
    bvhNode->bbox = primitiveInfos[ primBegin ].bbox;
    for ( uint32_t iPrim = primBegin + 1; iPrim < primEnd; ++iPrim )
    {
        DirectX::BoundingBox::CreateMerged( bvhNode->bbox, bvhNode->bbox, primitiveInfos[ iPrim ].bbox );
    }

    uint32_t primCount = primEnd - primBegin;
    if ( primCount == 1 )
    {
        uint32_t primIndex = primitiveInfos[ primBegin ].primIndex;
        reorderedTriangles[ reorderedTrianglesCount ] = triangles[ primIndex ];
        bvhNode->primIndex = reorderedTrianglesCount;
        bvhNode->primCount = 1;
        reorderedTrianglesCount += 1;
        return;
    }
    else
    {
        // Compute centroid bounding box
        DirectX::BoundingBox centroidBox;
        {
            DirectX::XMVECTOR vCentroidMin, vCentroidMax, vCentroid;
            vCentroidMin = DirectX::XMLoadFloat3( &primitiveInfos[ primBegin ].bbox.Center );
            vCentroidMax = vCentroidMin;
            for ( size_t i = primBegin + 1; i < primEnd; ++i )
            {
                vCentroid = DirectX::XMLoadFloat3( &primitiveInfos[ i ].bbox.Center );
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

        // Partition
        uint32_t primMiddle = ( primBegin + primEnd ) / 2;

        // Handle special occasion
        if ( ( (float*) ( &centroidBox.Extents ) )[ axis ] == 0.f )
        {
            if ( primCount < k_MaxPrimCountInNode )
            {
                for ( size_t iPrim = 0; iPrim < primCount; ++iPrim )
                {
                    size_t primIndex = primitiveInfos[ primBegin + iPrim ].primIndex;
                    reorderedTriangles[ reorderedTrianglesCount + iPrim ] = triangles[ primIndex ];
                }
                bvhNode->primIndex = reorderedTrianglesCount;
                bvhNode->primCount = uint8_t( primCount );
                reorderedTrianglesCount += primCount;
            }
            else
            {
                {
                    bvhNode->splitAxis = axis;
                    BuildNodeRecursively( primitiveInfos, vertices, triangles, primBegin, primMiddle, reorderedTriangles, reorderedTrianglesCount, bvhNodes );
                    bvhNode = &( *bvhNodes )[ bvhNodeIndex ];
                    bvhNode->childIndex = uint32_t( bvhNodes->size() );
                    BuildNodeRecursively( primitiveInfos, vertices, triangles, primMiddle, primEnd, reorderedTriangles, reorderedTrianglesCount, bvhNodes );
                }
            }
            return;
        }

        if ( primCount <= 4 )
        {
            // If primitives number is smaller than 4, devide equally
            std::nth_element( &primitiveInfos[ primBegin ], &primitiveInfos[ primMiddle ], &primitiveInfos[ primEnd - 1 ] + 1, PrimitiveInfo::BBoxCenterPerAxisLessPred( axis ) );
        }
        else
        {
            const size_t k_BucketsCount = 12;
            struct Bucket
            {
                uint32_t                primCount;
                DirectX::BoundingBox    bbox;
                Bucket() : primCount( 0 ) { }
            };
            Bucket buckets[ k_BucketsCount ];
            for ( size_t i = primBegin; i < primEnd; ++i )
            {
                float min = ( (float*) &centroidBox.Center )[ axis ] - ( (float*) &centroidBox.Extents )[ axis ];
                float size = ( (float*) &centroidBox.Extents )[ axis ] * 2.0f;
                uint32_t n = uint32_t( k_BucketsCount * ( ( ( (float*) &primitiveInfos[ i ].bbox.Center )[ axis ] ) - min ) / size );
                if ( n == k_BucketsCount ) --n;
                if ( buckets[ n ].primCount == 0 )
                {
                    buckets[ n ].bbox = primitiveInfos[ i ].bbox;
                }
                else
                {
                    DirectX::BoundingBox::CreateMerged( buckets[ n ].bbox, buckets[ n ].bbox, primitiveInfos[ i ].bbox );
                }
                assert( buckets[ n ].primCount < std::numeric_limits<decltype( Bucket::primCount )>::max() );
                buckets[ n ].primCount++;
            }

            // Compute cost for each splitting
            float cost[ k_BucketsCount - 1 ];
            for ( size_t i = 0; i < k_BucketsCount - 1; ++i )
            {
                uint32_t count0 = 0, count1 = 0;
                DirectX::BoundingBox b0 = buckets[ 0 ].bbox;
                for ( size_t j = 1; j <= i; ++j )
                {
                    DirectX::BoundingBox::CreateMerged( b0, b0, buckets[ j ].bbox );
                    count0 += buckets[ j ].primCount;
                }

                DirectX::BoundingBox b1 = buckets[ i + 1 ].bbox;
                for ( size_t j = i + 2; j < k_BucketsCount; ++j )
                {
                    DirectX::BoundingBox::CreateMerged( b1, b1, buckets[ j ].bbox );
                    count1 += buckets[ j ].primCount;
                }

                cost[ i ] = .125f + ( count0 * BoundingBoxSurfaceArea( b0 ) + count1 * BoundingBoxSurfaceArea( b1 ) )
                    / BoundingBoxSurfaceArea( bvhNode->bbox );
            }

            // Find smallest cost
            size_t minCostIndex = std::distance( std::begin( cost ), std::min_element( std::begin( cost ), std::end( cost ) ) );
            float minCost = cost[ minCostIndex ];

            if ( primCount > k_MaxPrimCountInNode || minCost < primCount )
            {
                PrimitiveInfo* prim = std::partition( &primitiveInfos[ primBegin ], &primitiveInfos[ primEnd - 1 ] + 1,
                                                      BucketLessEqualPred( minCostIndex, k_BucketsCount, axis, centroidBox ) );
                primMiddle = (uint32_t) std::distance( primitiveInfos.data(), prim );
            }
            else
            {
                // Create leaf node
                for ( size_t i = 0; i < primCount; ++i )
                {
                    size_t primIndex = primitiveInfos[ primBegin + i ].primIndex;
                    reorderedTriangles[ reorderedTrianglesCount + i ] = triangles[ primIndex ];
                }
                bvhNode->primIndex = reorderedTrianglesCount;
                bvhNode->primCount = uint8_t( primCount );
                reorderedTrianglesCount += primCount;
                return;
            }
        }

        {
            bvhNode->splitAxis = axis;
            BuildNodeRecursively( primitiveInfos, vertices, triangles, primBegin, primMiddle, reorderedTriangles, reorderedTrianglesCount, bvhNodes );
            bvhNode = &( *bvhNodes )[ bvhNodeIndex ];
            bvhNode->childIndex = uint32_t( bvhNodes->size() );
            BuildNodeRecursively( primitiveInfos, vertices, triangles, primMiddle, primEnd, reorderedTriangles, reorderedTrianglesCount, bvhNodes );
        }
    }
}

void BuildBVH( const Vertex* vertices, const uint32_t* indices, uint32_t* reorderedIndices, uint32_t triangleCount, std::vector<UnpackedBVHNode>* bvhNodes )
{
    std::vector<PrimitiveInfo> primitiveInfos;
    primitiveInfos.reserve( triangleCount );
    for ( uint32_t iPrim = 0; iPrim < triangleCount; ++iPrim )
    {
        uint32_t index0 = indices[ iPrim * 3 ];
        uint32_t index1 = indices[ iPrim * 3 + 1 ];
        uint32_t index2 = indices[ iPrim * 3 + 2 ];
        primitiveInfos.emplace_back();
        PrimitiveInfo& newBvhPrim = primitiveInfos.back();
        CalculateTriangleBoundingBox( vertices[ index0 ], vertices[ index1 ], vertices[ index2 ], &newBvhPrim.bbox );
        newBvhPrim.primIndex = iPrim;
    }

    uint32_t reorderedTrianglesCount = 0;
    BuildNodeRecursively( primitiveInfos, vertices, (TriangleIndices*) indices, 0, triangleCount, (TriangleIndices*) reorderedIndices, reorderedTrianglesCount, bvhNodes );
    assert( reorderedTrianglesCount == triangleCount );
}

void PackBVH( const UnpackedBVHNode* bvhNodes, uint32_t nodeCount, PackedBVHNode* packedBvhNodes )
{
    for ( uint32_t iNode = 0; iNode < nodeCount; ++iNode )
    {
        packedBvhNodes[ iNode ] = PackedBVHNode( bvhNodes[ iNode ] );
    }
}

PackedBVHNode::PackedBVHNode( const UnpackedBVHNode& unpacked )
    : childOrPrimIndex( unpacked.childIndex )
    , misc( unpacked.primCount )
{
    DirectX::XMVECTOR vCenter = DirectX::XMLoadFloat3( &unpacked.bbox.Center );
    DirectX::XMVECTOR vExtends = DirectX::XMLoadFloat3( &unpacked.bbox.Extents );
    DirectX::XMVECTOR vBBoxMin = DirectX::XMVectorSubtract( vCenter, vExtends );
    DirectX::XMVECTOR vBBoxMax = DirectX::XMVectorAdd( vCenter, vExtends );
    DirectX::XMStoreFloat3( &bboxMin, vBBoxMin );
    DirectX::XMStoreFloat3( &bboxMax, vBBoxMax );
}
