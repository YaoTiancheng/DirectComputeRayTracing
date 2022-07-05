#pragma once

#include "BVHAccel.h"
#include "../Shaders/Vertex.inc.hlsl"
#include "../Shaders/Material.inc.hlsl"
#include "../Shaders/BVHNode.inc.hlsl"

class Mesh
{
public:
    bool                    LoadFromOBJFile( const char* filename
                                           , const char* mtlFileDir
                                           , bool applyTransform = false
                                           , const DirectX::XMFLOAT4X4& transform = XMFLOAT4X4( 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f )
                                           , uint32_t materialIdOverride = -1 );

    void                    BuildBVH( const char* BVHFilename = nullptr );

    void                    Clear();

    uint32_t                GetVertexCount() const { return (uint32_t)m_Vertices.size(); }

    uint32_t                GetIndexCount() const { return (uint32_t)m_Indices.size(); }

    uint32_t                GetTriangleCount() const { return (uint32_t)GetIndexCount() / 3; }

    uint32_t                GetBVHNodeCount() const { return (uint32_t)m_BVHNodes.size(); }

    const Vertex*           GetVertices() const { return m_Vertices.data(); }

    const uint32_t*         GetIndices() const { return m_Indices.data(); }

    const BVHNode*          GetBVHNodes() const { return m_BVHNodes.data(); }

    uint32_t                GetBVHMaxDepth() const { return m_BVHMaxDepth; }

    uint32_t                GetBVHMaxStackSize() const { return m_BVHMaxStackSize; }

    const uint32_t*         GetMaterialIds() const { return m_MaterialIds.data(); }

    const std::vector<Material>&    GetMaterials() const { return m_Materials; }

    std::vector<Material>&  GetMaterials() { return m_Materials; }

    const std::vector<std::string>& GetMaterialNames() const { return m_MaterialNames; }

    std::vector<std::string>& GetMaterialNames() { return m_MaterialNames; }

private:
    std::vector<Vertex>         m_Vertices;
    std::vector<uint32_t>       m_Indices;
    std::vector<BVHNode>        m_BVHNodes;
    uint32_t                    m_BVHMaxDepth = 0;
    uint32_t                    m_BVHMaxStackSize = 0;
    std::vector<uint32_t>       m_MaterialIds;
    std::vector<Material>       m_Materials;
    std::vector<std::string>    m_MaterialNames;
};