#include "stdafx.h"
#include "Mesh.h"
#define TINYOBJLOADER_IMPLEMENTATION
#include "OBJLoader/tiny_obj_loader.h"

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
}

namespace std 
{
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

bool Mesh::LoadFromOBJFile( const char* filename, const char* mtlFileDir, bool buildBVH, const char* BVHFilename )
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn;
    std::string err;
    bool loadSuccessful = tinyobj::LoadObj( &attrib, &shapes, &materials, &warn, &err, filename, mtlFileDir );
    if ( !loadSuccessful )
    {
        return false;
    }

    std::unordered_map<tinyobj::index_t, uint32_t> tinyOBJIndexToVertexIndexMap;
    std::vector<XMFLOAT3> tangents;

    size_t normalCount = attrib.normals.size() / 3;
    if ( normalCount == 0 )
        return false;

    // Generate tangents for normals
    tangents.resize( normalCount );
    for ( size_t i = 0; i < normalCount; ++i )
    {
        XMFLOAT3 normal( attrib.normals[ i * 3 ], attrib.normals[ i * 3 + 1 ], attrib.normals[ i * 3 + 2 ] );
        XMVECTOR vNormal = XMLoadFloat3( &normal );
        XMVECTOR vDot = XMVector3Dot( vNormal, g_XMIdentityR1 );
        vDot = XMVectorAbs( vDot );
        XMVECTOR vHelper;
        if ( XMVector3NearEqual( vDot, g_XMOne, g_XMEpsilon ) )
        {
            vHelper = g_XMIdentityR0;
        }
        else
        {
            vHelper = g_XMIdentityR1;
        }

        XMVECTOR vBinormal = XMVector3Cross( vNormal, vHelper );
        vBinormal = XMVector3Normalize( vBinormal );
        XMStoreFloat3( tangents.data() + i, vBinormal );
    }

    std::vector<uint32_t> indices;
    std::vector<uint32_t> materialIds;
    bool needDefaultMaterial = false;

    for ( size_t iShapes = 0; iShapes < shapes.size(); ++iShapes )
    {
        tinyobj::mesh_t& mesh = shapes[ iShapes ].mesh;

        assert( mesh.num_face_vertices.size() == mesh.material_ids.size() );

        for ( size_t iFace = 0; iFace < mesh.num_face_vertices.size(); ++iFace )
        {
            assert( mesh.num_face_vertices[ iFace ] == 3 );

            int materialId = mesh.material_ids[ iFace ];
            // If the triangle has a material id out of range then assign a default material for it.
            if ( materialId < 0 || materialId >= materials.size() )
            {
                materialId = (int)materials.size();
                if ( !needDefaultMaterial )
                {
                    needDefaultMaterial = true;
                }
            }
            materialIds.push_back( materialId );

            for ( int iVertex = 0; iVertex < 3; ++iVertex )
            {
                tinyobj::index_t idx = mesh.indices[ iFace * 3 + iVertex ];

                if ( idx.vertex_index == -1 || idx.normal_index == -1 )
                    return false;

                size_t vertexIndex = 0;
                auto iter = tinyOBJIndexToVertexIndexMap.find( idx );
                if ( iter != tinyOBJIndexToVertexIndexMap.end() )
                {
                    vertexIndex = iter->second;
                }
                else
                {
                    if ( m_Vertices.size() == UINT_MAX )
                        return false;

                    vertexIndex = m_Vertices.size();
                    Vertex vertex;
                    vertex.position = XMFLOAT3( attrib.vertices[ idx.vertex_index * 3 ], attrib.vertices[ idx.vertex_index * 3 + 1 ], attrib.vertices[ idx.vertex_index * 3 + 2 ] );
                    vertex.normal   = XMFLOAT3( attrib.normals[ idx.normal_index * 3 ], attrib.normals[ idx.normal_index * 3 + 1 ], attrib.normals[ idx.normal_index * 3 + 2 ] );
                    vertex.tangent  = tangents[ idx.normal_index ];
                    m_Vertices.emplace_back( vertex );

                    tinyOBJIndexToVertexIndexMap.insert( std::make_pair( idx, vertexIndex ) );
                }
                indices.push_back( (uint32_t)vertexIndex );
            }
        }
    }

    m_Materials.reserve( materials.size() );
    for ( auto& iterSrcMat : materials )
    {
        Material dstMat;
        dstMat.albedo    = DirectX::XMFLOAT3( iterSrcMat.diffuse[ 0 ], iterSrcMat.diffuse[ 1 ], iterSrcMat.diffuse[ 2 ] );
        dstMat.emission  = DirectX::XMFLOAT3( iterSrcMat.emission[ 0 ], iterSrcMat.emission[ 1 ], iterSrcMat.emission[ 2 ] );
        dstMat.roughness = std::fmax( iterSrcMat.roughness, 0.00018f );
        dstMat.ior       = iterSrcMat.ior;
        m_Materials.emplace_back( dstMat );
    }

    if ( needDefaultMaterial )
    {
        Material defaultMat;
        defaultMat.albedo    = DirectX::XMFLOAT3( 1.0f, 0.0f, 1.0f );
        defaultMat.emission  = DirectX::XMFLOAT3( 0.0f, 0.0f, 0.0f );
        defaultMat.roughness = 1.0f;
        defaultMat.ior       = 1.5f;
        m_Materials.emplace_back( defaultMat );
    }

    if ( buildBVH )
    {
        m_Indices.resize( indices.size() );
        m_MaterialIds.resize( materialIds.size() );
        std::vector<UnpackedBVHNode> BVHNodes;
        BuildBVH( m_Vertices.data(), indices.data(), m_Indices.data(), materialIds.data(), m_MaterialIds.data(), GetTriangleCount(), &BVHNodes, &m_BVHMaxDepth );

        if ( BVHFilename )
        {
            FILE* file = fopen( BVHFilename, "w" );
            if ( file )
            {
                SerializeBVHToXML( BVHNodes.data(), file );
                fclose( file );
            }
        }

        m_BVHNodes.resize( BVHNodes.size() );
        PackBVH( BVHNodes.data(), uint32_t( BVHNodes.size() ), m_BVHNodes.data() );
    }
    else
    {
        m_Indices = indices;
        m_MaterialIds = materialIds;
    }

    return true;
}
