#pragma once

#include "Constants.h"
#include "BVHAccel.h"
#include "MathHelper.h"
#include "../Shaders/Vertex.inc.hlsl"
#include "../Shaders/Material.inc.hlsl"
#include "../Shaders/BVHNode.inc.hlsl"
#define TINYOBJLOADER_IMPLEMENTATION
#include "OBJLoader/tiny_obj_loader.h"

class Mesh
{
public:
    bool CreateFromWavefrontOBJData( const tinyobj::attrib_t& attrib, const std::vector<tinyobj::shape_t>& shapes, uint32_t materialIdBase, bool applyTransform = false,
        const DirectX::XMFLOAT4X4& transform = MathHelper::s_IdentityMatrix, uint32_t materialIdOverride = INVALID_MATERIAL_ID );

    bool GenerateRectangle( uint32_t materialId, bool applyTransform = false, const DirectX::XMFLOAT4X4& transform = MathHelper::s_IdentityMatrix );

    void BuildBVH( const char* BVHFilename = nullptr, std::vector<uint32_t>* reorderedTriangleIndices = nullptr );

    void Clear();

    uint32_t GetVertexCount() const { return (uint32_t)m_Vertices.size(); }

    uint32_t GetIndexCount() const { return (uint32_t)m_Indices.size(); }

    uint32_t GetTriangleCount() const { return (uint32_t)GetIndexCount() / 3; }

    uint32_t GetBVHNodeCount() const { return (uint32_t)m_BVHNodes.size(); }

    const std::vector<GPU::Vertex>& GetVertices() const { return m_Vertices; }

    std::vector<GPU::Vertex>& GetVertices() { return m_Vertices; }

    const std::vector<uint32_t>& GetIndices() const { return m_Indices; }

    std::vector<uint32_t>& GetIndices() { return m_Indices; }

    const BVHAccel::BVHNode* GetBVHNodes() const { return m_BVHNodes.data(); }

    uint32_t GetBVHMaxDepth() const { return m_BVHMaxDepth; }

    uint32_t GetBVHMaxStackSize() const { return m_BVHMaxStackSize; }

    const std::vector<uint32_t>& GetMaterialIds() const { return m_MaterialIds; }

    std::vector<uint32_t>& GetMaterialIds() { return m_MaterialIds; }

private:
    std::vector<GPU::Vertex> m_Vertices;
    std::vector<uint32_t> m_Indices;
    std::vector<BVHAccel::BVHNode> m_BVHNodes;
    uint32_t m_BVHMaxDepth = 0;
    uint32_t m_BVHMaxStackSize = 0;
    std::vector<uint32_t> m_MaterialIds;
};