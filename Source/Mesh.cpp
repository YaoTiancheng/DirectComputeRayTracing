#include "stdafx.h"
#include "Mesh.h"
#include "Logging.h"
#define TINYOBJLOADER_IMPLEMENTATION
#include "OBJLoader/tiny_obj_loader.h"
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
        static XMFLOAT2 s_DefaultTexcoords[ 3 ] = {
            { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f }
        };
        fvTexcOut[ 0 ] = s_DefaultTexcoords[ iVert ].x;
        fvTexcOut[ 1 ] = s_DefaultTexcoords[ iVert ].y;
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

    std::unordered_map<SVertexKey, uint32_t> vertexKeyToVertexIndexMap;

    size_t normalCount = attrib.normals.size() / 3;
    if ( normalCount == 0 )
        return false;

    bool hasTexcoord = !attrib.texcoords.empty();

    std::vector<uint32_t> indices;
    std::vector<uint32_t> materialIds;
    bool needDefaultMaterial = false;

    SMikkTSpaceContext mikkTSpaceContext;
    SMikkTSpaceInterface mikkTSpaceInterface;
    mikkTSpaceContext.m_pInterface = &mikkTSpaceInterface;
    ZeroMemory( &mikkTSpaceInterface, sizeof( SMikkTSpaceInterface ) );
    mikkTSpaceInterface.m_getNumFaces           = MikkTSpaceGetNumFaces;
    mikkTSpaceInterface.m_getNumVerticesOfFace  = MikkTSpaceGetNumVerticesOfFace;
    mikkTSpaceInterface.m_getPosition           = MikkTSpaceGetPosition;
    mikkTSpaceInterface.m_getNormal             = MikkTSpaceGetNormal;
    mikkTSpaceInterface.m_getTexCoord           = MikkTSpaceGetTexcoord;
    mikkTSpaceInterface.m_setTSpaceBasic        = MikkTSpaceSetTSpaceBasic;

    std::vector<XMFLOAT3> tangents;

    for ( size_t iShapes = 0; iShapes < shapes.size(); ++iShapes )
    {
        tinyobj::mesh_t& mesh = shapes[ iShapes ].mesh;

        assert( mesh.num_face_vertices.size() == mesh.material_ids.size() );

        if ( !GenerateTangentVectorsForMesh( attrib, mesh, &mikkTSpaceContext, &tangents ) )
        {
            LOG_STRING_FORMAT( "Generating tangent failed for mesh %s. This mesh was not loaded.\n", shapes[ iShapes ].name.c_str() );
            continue;
        }

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

                XMFLOAT3 tangent = tangents[ iFace * 3 + iVertex ];

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
                    Vertex vertex;
                    vertex.position = XMFLOAT3( attrib.vertices[ idx.vertex_index * 3 ], attrib.vertices[ idx.vertex_index * 3 + 1 ], attrib.vertices[ idx.vertex_index * 3 + 2 ] );
                    vertex.normal   = XMFLOAT3( attrib.normals[ idx.normal_index * 3 ], attrib.normals[ idx.normal_index * 3 + 1 ], attrib.normals[ idx.normal_index * 3 + 2 ] );
                    vertex.tangent  = tangent;
                    vertex.texcoord = hasTexcoord ? XMFLOAT2( attrib.texcoords[ idx.texcoord_index * 2 ], attrib.texcoords[ idx.texcoord_index * 2 + 1 ] ) : XMFLOAT2( 0.0f, 0.0f );

                    vertex.position.x = -vertex.position.x;
                    vertex.normal.x   = -vertex.normal.x;
                    vertex.tangent.x  = -vertex.tangent.x;

                    m_Vertices.emplace_back( vertex );

                    vertexKeyToVertexIndexMap.insert( std::make_pair( vertexKey, vertexIndex ) );
                }
                indices.push_back( vertexIndex );
            }
        }
    }

    m_Materials.reserve( materials.size() );
    m_MaterialNames.reserve( materials.size() );
    for ( auto& iterSrcMat : materials )
    {
        Material dstMat;
        dstMat.albedo    = DirectX::XMFLOAT3( iterSrcMat.diffuse[ 0 ], iterSrcMat.diffuse[ 1 ], iterSrcMat.diffuse[ 2 ] );
        dstMat.emission  = DirectX::XMFLOAT3( iterSrcMat.emission[ 0 ], iterSrcMat.emission[ 1 ], iterSrcMat.emission[ 2 ] );
        dstMat.roughness = iterSrcMat.roughness;
        dstMat.ior       = iterSrcMat.ior;
        dstMat.transmission = iterSrcMat.transmittance[ 0 ];
        dstMat.texTiling = XMFLOAT2( 1.0f, 1.0f );
        dstMat.flags     = 0;
        m_Materials.emplace_back( dstMat );
        m_MaterialNames.emplace_back( iterSrcMat.name );
    }

    if ( needDefaultMaterial )
    {
        Material defaultMat;
        defaultMat.albedo    = DirectX::XMFLOAT3( 1.0f, 0.0f, 1.0f );
        defaultMat.emission  = DirectX::XMFLOAT3( 0.0f, 0.0f, 0.0f );
        defaultMat.roughness = 1.0f;
        defaultMat.ior       = 1.5f;
        defaultMat.transmission = 0.0f;
        defaultMat.texTiling = XMFLOAT2( 1.0f, 1.0f );
        defaultMat.flags     = 0;
        m_Materials.emplace_back( defaultMat );
        m_MaterialNames.emplace_back( "Default Material" );
    }

    if ( buildBVH )
    {
        m_Indices.resize( indices.size() );
        m_MaterialIds.resize( materialIds.size() );
        std::vector<UnpackedBVHNode> BVHNodes;
        BuildBVH( m_Vertices.data(), indices.data(), m_Indices.data(), materialIds.data(), m_MaterialIds.data(), GetTriangleCount(), &BVHNodes, &m_BVHMaxDepth, &m_BVHMaxStackSize );

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
