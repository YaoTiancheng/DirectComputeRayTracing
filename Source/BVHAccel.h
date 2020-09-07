#pragma once

struct UnpackedBVHNode
{
    DirectX::BoundingBox    bbox;
    union 
    {
        uint32_t            childIndex;
        uint32_t            primIndex;
    };
    uint8_t                 primCount;
    uint8_t                 splitAxis;
};

struct PackedBVHNode
{
    DirectX::XMFLOAT3       bboxMin;
    DirectX::XMFLOAT3       bboxMax;
    uint32_t                childOrPrimIndex;
    uint32_t                misc;

    PackedBVHNode() = default;

    PackedBVHNode( const UnpackedBVHNode& unpacked );
};

struct Vertex;

void BuildBVH( const Vertex* vertices, const uint32_t* indicies, uint32_t* reorderedIndices, const uint32_t* triangleIds, uint32_t* reorderedTriangleIds, uint32_t triangleCount, std::vector<UnpackedBVHNode>* bvhNodes );

void PackBVH( const UnpackedBVHNode* bvhNodes, uint32_t nodeCount, PackedBVHNode* packedBvhNodes );