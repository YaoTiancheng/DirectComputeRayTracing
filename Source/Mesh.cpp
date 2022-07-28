#include "stdafx.h"
#include "Mesh.h"
#include "Logging.h"
#include "Constants.h"
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
    STinyObjMeshMikkTSpaceContext( const tinyobj::attrib_t& attrib, const tinyobj::mesh_t& mesh, const std::vector<bool>& isTexcoordDegenerated, std::vector<XMFLOAT3>* tangents )
        : m_Attrib( attrib ), m_Mesh( mesh ), m_IsTexcoordDegenerated( isTexcoordDegenerated ), m_Tangents( tangents )
    {}
    
    const tinyobj::attrib_t& m_Attrib;
    const tinyobj::mesh_t&   m_Mesh;
    const std::vector<bool>& m_IsTexcoordDegenerated;
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
    // Don't feed MikkTSpace with degenerated texcoord because otherwise it gives a tangent at (1,0,0). And when the normal vector is at (1,0,0) as well the tangent will be re-orthogonalized to be (0,0,0) in the shader.
    if ( idx.texcoord_index != -1 && !meshContext->m_IsTexcoordDegenerated[ iFace ] )
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


static bool GenerateTangentVectorsForMesh( const tinyobj::attrib_t& attrib, const tinyobj::mesh_t& mesh, SMikkTSpaceContext* context, const std::vector<bool>& isTexcoordDegenerated, std::vector<XMFLOAT3>* outTangents )
{
    outTangents->resize( mesh.num_face_vertices.size() * 3 );
    STinyObjMeshMikkTSpaceContext meshContext( attrib, mesh, isTexcoordDegenerated, outTangents );
    context->m_pUserData = &meshContext;
    return genTangSpaceDefault( context );
}

bool Mesh::LoadFromOBJFile( const char* filename, const char* mtlFileDir, bool applyTransform, const DirectX::XMFLOAT4X4& transform, uint32_t materialIdOverride )
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
    std::vector<bool> isTexcoordDegenerated;

    XMMATRIX vTransform, vNormalTransform;
    if ( applyTransform )
    {
        vTransform = XMLoadFloat4x4( &transform );
        XMVECTOR vDet;
        vNormalTransform = XMMatrixTranspose( XMMatrixInverse( &vDet, vTransform ) );
    }

    for ( size_t iShapes = 0; iShapes < shapes.size(); ++iShapes )
    {
        tinyobj::mesh_t& mesh = shapes[ iShapes ].mesh;

        assert( mesh.num_face_vertices.size() == mesh.material_ids.size() );

        // Mark faces who has degenerated texcoords. ( The magnitude of the cross product from 2 edge vectors nears 0. )
        isTexcoordDegenerated.resize( mesh.num_face_vertices.size() );
        for ( size_t iFace = 0; iFace < mesh.num_face_vertices.size(); ++iFace )
        {
            tinyobj::index_t idx0 = mesh.indices[ iFace * 3 ];
            tinyobj::index_t idx1 = mesh.indices[ iFace * 3 + 1 ];
            tinyobj::index_t idx2 = mesh.indices[ iFace * 3 + 2 ];
            XMFLOAT2 uv0 = idx0.texcoord_index != -1 ? XMFLOAT2( attrib.texcoords[ idx0.texcoord_index * 2 ], attrib.texcoords[ idx0.texcoord_index * 2 + 1 ] ) : XMFLOAT2( 0.0f, 0.0f );
            XMFLOAT2 uv1 = idx1.texcoord_index != -1 ? XMFLOAT2( attrib.texcoords[ idx1.texcoord_index * 2 ], attrib.texcoords[ idx1.texcoord_index * 2 + 1 ] ) : XMFLOAT2( 0.0f, 0.0f );
            XMFLOAT2 uv2 = idx2.texcoord_index != -1 ? XMFLOAT2( attrib.texcoords[ idx2.texcoord_index * 2 ], attrib.texcoords[ idx2.texcoord_index * 2 + 1 ] ) : XMFLOAT2( 0.0f, 0.0f );
            XMFLOAT2 e0 = XMFLOAT2( uv1.x - uv0.x, uv1.y - uv0.y );
            XMFLOAT2 e1 = XMFLOAT2( uv2.x - uv0.x, uv2.y - uv0.y );
            float z = e0.x * e1.y - e0.y * e1.x; // The Z component of the cross product.
            isTexcoordDegenerated[ iFace ] = fabsf( z ) < 0.000001f;
        }

        if ( !GenerateTangentVectorsForMesh( attrib, mesh, &mikkTSpaceContext, isTexcoordDegenerated, &tangents ) )
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
                // If the triangle has a material id out of range then assign a default material for it.
                if ( materialId < 0 || materialId >= materials.size() )
                {
                    materialId = (int)materials.size();
                    if ( !needDefaultMaterial )
                    {
                        needDefaultMaterial = true;
                    }
                }
                m_MaterialIds.push_back( uint32_t( m_Materials.size() + materialId ) );
            }
            else
            {
                m_MaterialIds.push_back( materialIdOverride );
            }

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

    if ( !hasMaterialOverride )
    {
        m_Materials.reserve( m_Materials.size() + materials.size() );
        m_MaterialNames.reserve( m_MaterialNames.size() + materials.size() );
        for ( auto& iterSrcMat : materials )
        {
            SMaterialSetting dstMat;
            dstMat.m_Albedo = DirectX::XMFLOAT3( iterSrcMat.diffuse[ 0 ], iterSrcMat.diffuse[ 1 ], iterSrcMat.diffuse[ 2 ] );
            dstMat.m_Emission = DirectX::XMFLOAT3( iterSrcMat.emission[ 0 ], iterSrcMat.emission[ 1 ], iterSrcMat.emission[ 2 ] );
            dstMat.m_Roughness = iterSrcMat.roughness;
            dstMat.m_IOR = XMFLOAT3( std::clamp( iterSrcMat.ior, 1.0f, MAX_MATERIAL_IOR ), 1.f, 1.f );
            dstMat.m_K = XMFLOAT3( 1.0f, 1.0f, 1.0f );
            dstMat.m_Transmission = 1.0f - iterSrcMat.dissolve;
            dstMat.m_Tiling = XMFLOAT2( 1.0f, 1.0f );
            dstMat.m_IsMetal = false;
            dstMat.m_HasAlbedoTexture = false;
            dstMat.m_HasEmissionTexture = false;
            dstMat.m_HasRoughnessTexture = false;
            m_Materials.emplace_back( dstMat );
            m_MaterialNames.emplace_back( iterSrcMat.name );
        }

        if ( needDefaultMaterial )
        {
            SMaterialSetting defaultMat;
            defaultMat.m_Albedo = DirectX::XMFLOAT3( 1.0f, 0.0f, 1.0f );
            defaultMat.m_Emission = DirectX::XMFLOAT3( 0.0f, 0.0f, 0.0f );
            defaultMat.m_Roughness = 1.0f;
            defaultMat.m_IOR = XMFLOAT3( 1.5f, 1.f, 1.f );
            defaultMat.m_K = XMFLOAT3( 1.0f, 1.0f, 1.0f );
            defaultMat.m_Transmission = 0.0f;
            defaultMat.m_Tiling = XMFLOAT2( 1.0f, 1.0f );
            defaultMat.m_IsMetal = false;
            defaultMat.m_HasAlbedoTexture = false;
            defaultMat.m_HasEmissionTexture = false;
            defaultMat.m_HasRoughnessTexture = false;
            m_Materials.emplace_back( defaultMat );
            m_MaterialNames.emplace_back( "Default Material" );
        }
    }

    return true;
}

bool Mesh::GenerateRectangle( uint32_t materialId, bool applyTransform, const DirectX::XMFLOAT4X4& transform )
{
    if ( GetVertexCount() + 4 <= UINT_MAX )
    {
        Vertex vertices[ 4 ] =
        {
              { { 1.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f }, { 1.0f, 1.0f } }
            , { { 1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f } }
            , { { -1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f } }
            , { { -1.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f } }
        };
        uint32_t indices[ 6 ] = { 0, 1, 3, 1, 2, 3 };

        uint32_t indexBase = GetVertexCount();

        m_Vertices.reserve( m_Vertices.size() + 4 );
        XMMATRIX vTransform = XMLoadFloat4x4( &transform );
        XMVECTOR vDet;
        XMMATRIX vNormalTransform = XMMatrixTranspose( XMMatrixInverse( &vDet, vTransform ) );
        for ( auto& vertex : vertices )
        {
            m_Vertices.emplace_back();
            Vertex& meshVertex = m_Vertices.back();
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

void Mesh::BuildBVH( const char* BVHFilename )
{
    std::vector<uint32_t> indices = m_Indices;
    std::vector<uint32_t> materialIds = m_MaterialIds;
    std::vector<UnpackedBVHNode> BVHNodes;
    ::BuildBVH( m_Vertices.data(), indices.data(), m_Indices.data(), materialIds.data(), m_MaterialIds.data(), GetTriangleCount(), &BVHNodes, &m_BVHMaxDepth, &m_BVHMaxStackSize );

    if ( BVHFilename && BVHFilename[ 0 ] != '\0' )
    {
        FILE* file = fopen( BVHFilename, "w" );
        if ( file )
        {
            SerializeBVHToXML( BVHNodes.data(), file );
            fclose( file );
        }
    }

    m_BVHNodes.resize( BVHNodes.size() );
    ::PackBVH( BVHNodes.data(), uint32_t( BVHNodes.size() ), m_BVHNodes.data() );
}

void Mesh::Clear()
{
    m_Vertices.clear();
    m_Indices.clear();
    m_MaterialIds.clear();
    m_MaterialNames.clear();
    m_Materials.clear();
    m_BVHNodes.clear();
    m_BVHMaxDepth = 0;
    m_BVHMaxStackSize = 0;
}


