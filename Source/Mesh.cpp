#include "stdafx.h"
#include "Mesh.h"
#include "Logging.h"
#include "Constants.h"
#include "MikkTSpace/mikktspace.h"

inline void hash_combine( std::size_t& seed ) { }

template <typename T, typename... Rest>
inline void hash_combine( std::size_t& seed, const T& v, Rest... rest ) 
{
    std::hash<T> hasher;
    seed ^= hasher( v ) + 0x9e3779b9 + ( seed << 6 ) + ( seed >> 2 );
    hash_combine( seed, rest... );
}

namespace std
{
    template<> struct hash<tinyobj::index_t>
    {
        std::size_t operator()( tinyobj::index_t const& s ) const noexcept
        {
            size_t h = 0;
            hash_combine( h, s.vertex_index, s.normal_index, s.texcoord_index );
            return h;
        }
    };

    template<> struct equal_to<tinyobj::index_t> 
    {
        bool operator()( const tinyobj::index_t& lhs, const tinyobj::index_t& rhs ) const 
        {
            return lhs.vertex_index == rhs.vertex_index
                && lhs.normal_index == rhs.normal_index
                && lhs.texcoord_index == rhs.texcoord_index;
        }
    };
}

using namespace DirectX;

struct SVertexKey
{
    tinyobj::index_t m_TinyObjIndex;
    XMFLOAT3         m_Tangent;
};

namespace std
{
    template<> struct hash<SVertexKey>
    {
        std::size_t operator()( SVertexKey const& s ) const noexcept
        {
            size_t h = 0;
            hash_combine( h, s.m_TinyObjIndex, s.m_Tangent.x, s.m_Tangent.y, s.m_Tangent.z );
            return h;
        }
    };

    template<> struct equal_to<SVertexKey>
    {
        bool operator()( const SVertexKey& lhs, const SVertexKey& rhs ) const
        {
            std::equal_to<tinyobj::index_t> equal_to;
            return equal_to( lhs.m_TinyObjIndex, rhs.m_TinyObjIndex )
                && lhs.m_Tangent.x == rhs.m_Tangent.x
                && lhs.m_Tangent.y == rhs.m_Tangent.y
                && lhs.m_Tangent.z == rhs.m_Tangent.z;
        }
    };
}

struct STinyObjMeshMikkTSpaceContext
{
    STinyObjMeshMikkTSpaceContext( const tinyobj::attrib_t& attrib, const tinyobj::mesh_t& mesh, std::vector<XMFLOAT3>* tangents )
        : m_Attrib( attrib ), m_Mesh( mesh ), m_Tangents( tangents )
    {}
    
    const tinyobj::attrib_t& m_Attrib;
    const tinyobj::mesh_t&   m_Mesh;
    std::vector<XMFLOAT3>*   m_Tangents;
};

static int MikkTSpaceGetNumFaces( const SMikkTSpaceContext* pContext )
{
    STinyObjMeshMikkTSpaceContext* meshContext = (STinyObjMeshMikkTSpaceContext*)pContext->m_pUserData;
    assert( meshContext->m_Mesh.num_face_vertices.size() <= std::numeric_limits<int>::max() );
    return (int)meshContext->m_Mesh.num_face_vertices.size();
}

static int MikkTSpaceGetNumVerticesOfFace( const SMikkTSpaceContext* pContext, const int iFace )
{
    STinyObjMeshMikkTSpaceContext* meshContext = (STinyObjMeshMikkTSpaceContext*)pContext->m_pUserData;
    assert( meshContext->m_Mesh.num_face_vertices[ iFace ] == 3 );
    return 3;
}

static void MikkTSpaceGetPosition( const SMikkTSpaceContext* pContext, float fvPosOut[], const int iFace, const int iVert )
{
    STinyObjMeshMikkTSpaceContext* meshContext = (STinyObjMeshMikkTSpaceContext*)pContext->m_pUserData;
    tinyobj::index_t idx = meshContext->m_Mesh.indices[ iFace * 3 + iVert ];
    fvPosOut[ 0 ] = meshContext->m_Attrib.vertices[ idx.vertex_index * 3 ];
    fvPosOut[ 1 ] = meshContext->m_Attrib.vertices[ idx.vertex_index * 3 + 1 ];
    fvPosOut[ 2 ] = meshContext->m_Attrib.vertices[ idx.vertex_index * 3 + 2 ];
}

static void MikkTSpaceGetNormal( const SMikkTSpaceContext* pContext, float fvNormOut[], const int iFace, const int iVert )
{
    STinyObjMeshMikkTSpaceContext* meshContext = (STinyObjMeshMikkTSpaceContext*)pContext->m_pUserData;
    tinyobj::index_t idx = meshContext->m_Mesh.indices[ iFace * 3 + iVert ];
    fvNormOut[ 0 ] = meshContext->m_Attrib.normals[ idx.normal_index * 3 ];
    fvNormOut[ 1 ] = meshContext->m_Attrib.normals[ idx.normal_index * 3 + 1 ];
    fvNormOut[ 2 ] = meshContext->m_Attrib.normals[ idx.normal_index * 3 + 2 ];
}

static void MikkTSpaceGetTexcoord( const SMikkTSpaceContext* pContext, float fvTexcOut[], const int iFace, const int iVert )
{
    STinyObjMeshMikkTSpaceContext* meshContext = (STinyObjMeshMikkTSpaceContext*)pContext->m_pUserData;
    tinyobj::index_t idx = meshContext->m_Mesh.indices[ iFace * 3 + iVert ];
    
    if ( idx.texcoord_index != -1 )
    { 
        fvTexcOut[ 0 ] = meshContext->m_Attrib.texcoords[ idx.texcoord_index * 2 ];
        fvTexcOut[ 1 ] = meshContext->m_Attrib.texcoords[ idx.texcoord_index * 2 + 1 ];
    }
    else
    {
        fvTexcOut[ 0 ] = 0.f;
        fvTexcOut[ 1 ] = 0.f;
    }
}

static void MikkTSpaceSetTSpaceBasic( const SMikkTSpaceContext* pContext, const float fvTangent[], const float fSign, const int iFace, const int iVert )
{
    STinyObjMeshMikkTSpaceContext* meshContext = (STinyObjMeshMikkTSpaceContext*)pContext->m_pUserData;
    ( *meshContext->m_Tangents )[ iFace * 3 + iVert ] = XMFLOAT3( fvTangent[ 0 ], fvTangent[ 1 ], fvTangent[ 2 ] );
}


static bool GenerateTangentVectorsForMesh( const tinyobj::attrib_t& attrib, const tinyobj::mesh_t& mesh, SMikkTSpaceContext* context, std::vector<XMFLOAT3>* outTangents )
{
    outTangents->resize( mesh.num_face_vertices.size() * 3 );
    STinyObjMeshMikkTSpaceContext meshContext( attrib, mesh, outTangents );
    context->m_pUserData = &meshContext;
    return genTangSpaceDefault( context );
}

bool Mesh::CreateFromWavefrontOBJData( const tinyobj::attrib_t& attrib, const std::vector<tinyobj::shape_t>& shapes, uint32_t materialIdBase, bool applyTransform, const DirectX::XMFLOAT4X4& transform, bool changeWindingOrder, uint32_t materialIdOverride )
{
    std::unordered_map<SVertexKey, uint32_t> vertexKeyToVertexIndexMap;

    size_t normalCount = attrib.normals.size() / 3;
    if ( normalCount == 0 )
        return false;

    bool hasMaterialOverride = materialIdOverride != -1;
    bool needDefaultMaterial = false;

    SMikkTSpaceContext mikkTSpaceContext;
    SMikkTSpaceInterface mikkTSpaceInterface;
    mikkTSpaceContext.m_pInterface = &mikkTSpaceInterface;
    ZeroMemory( &mikkTSpaceInterface, sizeof( SMikkTSpaceInterface ) );
    mikkTSpaceInterface.m_getNumFaces = MikkTSpaceGetNumFaces;
    mikkTSpaceInterface.m_getNumVerticesOfFace = MikkTSpaceGetNumVerticesOfFace;
    mikkTSpaceInterface.m_getPosition = MikkTSpaceGetPosition;
    mikkTSpaceInterface.m_getNormal = MikkTSpaceGetNormal;
    mikkTSpaceInterface.m_getTexCoord = MikkTSpaceGetTexcoord;
    mikkTSpaceInterface.m_setTSpaceBasic = MikkTSpaceSetTSpaceBasic;

    std::vector<XMFLOAT3> tangents;

    XMMATRIX vTransform, vNormalTransform;
    if ( applyTransform )
    {
        vTransform = XMLoadFloat4x4( &transform );
        XMVECTOR vDet;
        vNormalTransform = XMMatrixTranspose( XMMatrixInverse( &vDet, vTransform ) );
    }

    // For winding order selection
    const int originalTriangleIndices[ 3 ] = { 0, 1, 2 };
    const int changedTriangleIndices[ 3 ] = { 0, 2, 1 };
    const int* triangleIndices = changeWindingOrder ? changedTriangleIndices : originalTriangleIndices;

    for ( size_t iShapes = 0; iShapes < shapes.size(); ++iShapes )
    {
        const tinyobj::mesh_t& mesh = shapes[ iShapes ].mesh;

        assert( mesh.num_face_vertices.size() == mesh.material_ids.size() );

        if ( !GenerateTangentVectorsForMesh( attrib, mesh, &mikkTSpaceContext, &tangents ) )
        {
            LOG_STRING_FORMAT( "Generating tangent failed for mesh %s. This mesh was not loaded.\n", shapes[ iShapes ].name.c_str() );
            continue;
        }

        for ( size_t iFace = 0; iFace < mesh.num_face_vertices.size(); ++iFace )
        {
            assert( mesh.num_face_vertices[ iFace ] == 3 );

            if ( !hasMaterialOverride )
            {
                int materialId = mesh.material_ids[ iFace ];
                m_MaterialIds.push_back( materialId != -1 ? materialIdBase + uint32_t( materialId ) : INVALID_MATERIAL_ID );
            }
            else
            {
                m_MaterialIds.push_back( materialIdOverride );
            }

            for ( int iVertex = 0; iVertex < 3; ++iVertex )
            {
                tinyobj::index_t idx = mesh.indices[ iFace * 3 + triangleIndices[ iVertex ] ];

                if ( idx.vertex_index == -1 || idx.normal_index == -1 )
                    return false;

                XMFLOAT3 tangent = tangents[ iFace * 3 + triangleIndices[ iVertex ] ];

                SVertexKey vertexKey = { idx, tangent };
                uint32_t vertexIndex = 0;
                auto iter = vertexKeyToVertexIndexMap.find( vertexKey );
                if ( iter != vertexKeyToVertexIndexMap.end() )
                {
                    vertexIndex = iter->second;
                }
                else
                {
                    if ( m_Vertices.size() == UINT_MAX )
                        return false;

                    vertexIndex = (uint32_t)m_Vertices.size();
                    GPU::Vertex vertex;
                    vertex.position = XMFLOAT3( attrib.vertices[ idx.vertex_index * 3 ], attrib.vertices[ idx.vertex_index * 3 + 1 ], attrib.vertices[ idx.vertex_index * 3 + 2 ] );
                    vertex.normal = XMFLOAT3( attrib.normals[ idx.normal_index * 3 ], attrib.normals[ idx.normal_index * 3 + 1 ], attrib.normals[ idx.normal_index * 3 + 2 ] );
                    vertex.tangent = tangent;
                    vertex.texcoord = idx.texcoord_index != -1 ? XMFLOAT2( attrib.texcoords[ idx.texcoord_index * 2 ], attrib.texcoords[ idx.texcoord_index * 2 + 1 ] ) : XMFLOAT2( 0.0f, 0.0f );

                    if ( applyTransform )
                    {
                        XMVECTOR vPosition = XMLoadFloat3( &vertex.position );
                        XMVECTOR vNormal = XMLoadFloat3( &vertex.normal );
                        XMVECTOR vTangent = XMLoadFloat3( &vertex.tangent );
                        vPosition = XMVector3Transform( vPosition, vTransform );
                        vNormal = XMVector3TransformNormal( vNormal, vNormalTransform );
                        vTangent = XMVector3TransformNormal( vTangent, vNormalTransform );
                        XMStoreFloat3( &vertex.position, vPosition );
                        XMStoreFloat3( &vertex.normal, vNormal );
                        XMStoreFloat3( &vertex.tangent, vTangent );
                    }

                    m_Vertices.emplace_back( vertex );

                    vertexKeyToVertexIndexMap.insert( std::make_pair( vertexKey, vertexIndex ) );
                }
                m_Indices.push_back( vertexIndex );
            }
        }
    }

    return true;
}

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


