#include "stdafx.h"
#include "Mesh.h"
#include "Logging.h"

using namespace DirectX;

bool Mesh::GenerateRectangle( uint32_t materialId, bool applyTransform, const DirectX::XMFLOAT4X4& transform )
{
    if ( GetVertexCount() + 4 <= UINT_MAX )
    {
        GPU::Vertex vertices[ 4 ] =
        {
              { { 1.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f }, { 1.0f, 1.0f } }
            , { { 1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f } }
            , { { -1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f } }
            , { { -1.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f } }
        };
        uint32_t indices[ 6 ] = { 0, 1, 3, 1, 2, 3 }; // Front face winding order CCW

        uint32_t indexBase = GetVertexCount();

        m_Vertices.reserve( m_Vertices.size() + 4 );
        XMMATRIX vTransform = XMLoadFloat4x4( &transform );
        XMVECTOR vDet;
        XMMATRIX vNormalTransform = XMMatrixTranspose( XMMatrixInverse( &vDet, vTransform ) );
        for ( auto& vertex : vertices )
        {
            m_Vertices.emplace_back();
            GPU::Vertex& meshVertex = m_Vertices.back();
            XMVECTOR vPosition = XMLoadFloat3( &vertex.position );
            XMVECTOR vNormal = XMLoadFloat3( &vertex.normal );
            XMVECTOR vTangent = XMLoadFloat3( &vertex.tangent );
            vPosition = XMVector3Transform( vPosition, vTransform );
            vNormal = XMVector3TransformNormal( vNormal, vNormalTransform );
            vTangent = XMVector3TransformNormal( vTangent, vNormalTransform );
            XMStoreFloat3( &meshVertex.position, vPosition );
            XMStoreFloat3( &meshVertex.normal, vNormal );
            XMStoreFloat3( &meshVertex.tangent, vTangent );
        }

        m_Indices.reserve( indexBase + 6 );
        for ( auto index : indices )
        {
            m_Indices.push_back( indexBase + index );
        }

        m_MaterialIds.reserve( m_MaterialIds.size() + 2 );
        m_MaterialIds.push_back( materialId );
        m_MaterialIds.push_back( materialId );
        return true;
    }
    else
    {
        LOG_STRING( "Trying to generate a rectangle shape but maximum vertex count is exceeded.\n" );
        return false;
    }
}

void Mesh::BuildBVH( std::vector<uint32_t>* reorderedTriangleIndices )
{
    std::vector<uint32_t> indices = m_Indices;
    std::vector<uint32_t> triangleIndices;
    std::vector<uint32_t>* reorderedTriangleIndicesUsed = reorderedTriangleIndices;
    if ( !reorderedTriangleIndices )
    {
        reorderedTriangleIndicesUsed = &triangleIndices;
    }
    reorderedTriangleIndicesUsed->resize( GetTriangleCount() );
    BVHAccel::BuildBLAS( m_Vertices.data(), indices.data(), m_Indices.data(), reorderedTriangleIndicesUsed->data(), GetTriangleCount(), &m_BVHNodes, &m_BVHMaxDepth, &m_BVHMaxStackSize );

    // Reorder material id
    {
        std::vector<uint32_t> materialIds = m_MaterialIds;
        for ( size_t i = 0; i < materialIds.size(); ++i )
        {
            m_MaterialIds[ i ] = materialIds[ (*reorderedTriangleIndicesUsed)[ i ] ];
        }
    }
}

void Mesh::Clear()
{
    m_Vertices.clear();
    m_Indices.clear();
    m_MaterialIds.clear();
    m_BVHNodes.clear();
    m_BVHMaxDepth = 0;
    m_BVHMaxStackSize = 0;
}


