#pragma once

#include "BVHAccel.h"
#include "MathHelper.h"
#include "../Shaders/Vertex.inc.hlsl"
#include "../Shaders/Material.inc.hlsl"
#include "../Shaders/BVHNode.inc.hlsl"

struct SMaterialSetting
{
    DirectX::XMFLOAT3 m_Albedo;
    DirectX::XMFLOAT3 m_Emission;
    float m_Roughness;
    DirectX::XMFLOAT3 m_IOR;
    DirectX::XMFLOAT3 m_K;
    float m_Transmission;
    DirectX::XMFLOAT2 m_Tiling;
    bool m_IsMetal;
    bool m_HasAlbedoTexture;
    bool m_HasRoughnessTexture;
    bool m_HasEmissionTexture;
};

class Mesh
{
public:
    bool LoadFromOBJFile( const char* filename, const char* mtlFileDir, bool applyTransform = false, 
        const DirectX::XMFLOAT4X4& transform = MathHelper::s_IdentityMatrix, uint32_t materialIdOverride = -1 );

    bool GenerateRectangle( uint32_t materialId, bool applyTransform = false, const DirectX::XMFLOAT4X4& transform = MathHelper::s_IdentityMatrix );

    void BuildBVH( const char* BVHFilename = nullptr, std::vector<uint32_t>* reorderedTriangleIds = nullptr );

    void Clear();

    uint32_t GetVertexCount() const { return (uint32_t)m_Vertices.size(); }

    uint32_t GetIndexCount() const { return (uint32_t)m_Indices.size(); }

    uint32_t GetTriangleCount() const { return (uint32_t)GetIndexCount() / 3; }

    uint32_t GetBVHNodeCount() const { return (uint32_t)m_BVHNodes.size(); }

    const std::vector<GPU::Vertex>& GetVertices() const { return m_Vertices; }

    std::vector<GPU::Vertex>& GetVertices() { return m_Vertices; }

    const std::vector<uint32_t>& GetIndices() const { return m_Indices; }

    std::vector<uint32_t>& GetIndices() { return m_Indices; }

    const GPU::BVHNode* GetBVHNodes() const { return m_BVHNodes.data(); }

    uint32_t GetBVHMaxDepth() const { return m_BVHMaxDepth; }

    uint32_t GetBVHMaxStackSize() const { return m_BVHMaxStackSize; }

    const std::vector<uint32_t>& GetMaterialIds() const { return m_MaterialIds; }

    std::vector<uint32_t>& GetMaterialIds() { return m_MaterialIds; }

    const std::vector<SMaterialSetting>& GetMaterials() const { return m_Materials; }

    std::vector<SMaterialSetting>& GetMaterials() { return m_Materials; }

    const std::vector<std::string>& GetMaterialNames() const { return m_MaterialNames; }

    std::vector<std::string>& GetMaterialNames() { return m_MaterialNames; }

private:
    std::vector<GPU::Vertex> m_Vertices;
    std::vector<uint32_t> m_Indices;
    std::vector<GPU::BVHNode> m_BVHNodes;
    uint32_t m_BVHMaxDepth = 0;
    uint32_t m_BVHMaxStackSize = 0;
    std::vector<uint32_t> m_MaterialIds;
    std::vector<SMaterialSetting> m_Materials;
    std::vector<std::string> m_MaterialNames;
};