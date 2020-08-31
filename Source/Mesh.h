#pragma once

#include "Primitive.h"
#include "BVHAccel.h"

class Mesh
{
public:
    bool                    LoadFromOBJFile( const char* filename );

    uint32_t                GetVertexCount() const { return (uint32_t)m_Vertices.size(); }

    uint32_t                GetIndexCount() const { return (uint32_t)m_Indices.size(); }

    uint32_t                GetTriangleCount() const { return (uint32_t)GetIndexCount() / 3; }

    uint32_t                GetBVHNodeCount() const { return (uint32_t)m_BVHNodes.size(); }

    const Vertex*           GetVertices() const { return m_Vertices.data(); }

    const uint32_t*         GetIndices() const { return m_Indices.data(); }

    const PackedBVHNode*    GetBVHNodes() const { return m_BVHNodes.data(); }

private:
    std::vector<Vertex>         m_Vertices;
    std::vector<uint32_t>       m_Indices;
    std::vector<PackedBVHNode>  m_BVHNodes;
};