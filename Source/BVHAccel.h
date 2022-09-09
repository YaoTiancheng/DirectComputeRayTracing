#pragma once

namespace GPU
{
    struct Vertex;
    struct BVHNode;
}

namespace BVHAccel
{

struct BVHNode
{
    DirectX::BoundingBox m_BoundingBox;
    union 
    {
        uint32_t m_ChildIndex;
        uint32_t m_PrimIndex;
    };
    uint8_t m_PrimCount;
    uint8_t m_SplitAxis;
};

struct SInstance
{
    DirectX::BoundingBox m_BoundingBox;
    DirectX::XMFLOAT4X3 m_Transform;
};

void BuildBLAS( const GPU::Vertex* vertices, const uint32_t* indicies, uint32_t* reorderedIndices, uint32_t* reorderedTriangleIndices, uint32_t triangleCount
    , std::vector<BVHNode>* BVHNodes, uint32_t* maxDepth, uint32_t* maxStackSize );

void BuildTLAS( const SInstance* instances, uint32_t* reorderedInstanceIndices, uint32_t instanceCount, std::vector<BVHNode>* BVHNodes
    , uint32_t* maxDepth, uint32_t* maxStackSize );

void PackBVH( const BVHNode* bvhNodes, uint32_t nodeCount, bool isBLAS, GPU::BVHNode* packedBvhNodes );

void SerializeBVHToXML( const BVHNode* rootNode, FILE* file );

}