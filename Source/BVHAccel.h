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

struct Vertex;
struct BVHNode;

void BuildBVH( const Vertex* vertices, const uint32_t* indicies, uint32_t* reorderedIndices, const uint32_t* triangleIds, uint32_t* reorderedTriangleIds, uint32_t triangleCount, std::vector<UnpackedBVHNode>* bvhNodes, uint32_t* maxDepth, uint32_t* maxStackSize );

void PackBVH( const UnpackedBVHNode* bvhNodes, uint32_t nodeCount, BVHNode* packedBvhNodes );

void SerializeBVHToXML( const UnpackedBVHNode* rootNode, FILE* file );