#pragma once

#include "Primitive.h"
#include "BVHAccel.h"

struct Material
{
    DirectX::XMFLOAT3 albedo;
    DirectX::XMFLOAT3 emission;
    float             roughness;
    float             ior;
};

class Mesh
{
public:
    bool                    LoadFromOBJFile( const char* filename, const char* mtlFileDir, bool buildBVH = true );

    uint32_t                GetVertexCount() const { return (uint32_t)m_Vertices.size(); }

    uint32_t                GetIndexCount() const { return (uint32_t)m_Indices.size(); }

    uint32_t                GetTriangleCount() const { return (uint32_t)GetIndexCount() / 3; }

    uint32_t                GetBVHNodeCount() const { return (uint32_t)m_BVHNodes.size(); }

    uint32_t                GetMaterialCount() const { return (uint32_t)m_Materials.size(); }

    const Vertex*           GetVertices() const { return m_Vertices.data(); }

    const uint32_t*         GetIndices() const { return m_Indices.data(); }

    const PackedBVHNode*    GetBVHNodes() const { return m_BVHNodes.data(); }

    const uint32_t*         GetMaterialIds() const { return m_MaterialIds.data(); }

    const Material*         GetMaterials() const { return m_Materials.data(); }

private:
    std::vector<Vertex>         m_Vertices;
    std::vector<uint32_t>       m_Indices;
    std::vector<PackedBVHNode>  m_BVHNodes;
    std::vector<uint32_t>       m_MaterialIds;
    std::vector<Material>       m_Materials;
};