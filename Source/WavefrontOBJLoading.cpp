#include "stdafx.h"
#define TINYOBJLOADER_IMPLEMENTATION
#include "tinyobjloader/tiny_obj_loader.h"
#include "MikkTSpace/mikktspace.h"
#include "Scene.h"
#include "Logging.h"
#include "Constants.h"
#include "MathHelper.h"

inline static void hash_combine( std::size_t& seed ) { }

template <typename T, typename... Rest>
inline static void hash_combine( std::size_t& seed, const T& v, Rest... rest )
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
    XMFLOAT3 m_Tangent;
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
    STinyObjMeshMikkTSpaceContext( const tinyobj::attrib_t& attrib, const tinyobj::mesh_t& mesh, std::vector<XMFLOAT3>* tangents, bool flipTexcoordV )
        : m_Attrib( attrib ), m_Mesh( mesh ), m_Tangents( tangents ), m_FlipTexcoordV( flipTexcoordV )
    {}

    const tinyobj::attrib_t& m_Attrib;
    const tinyobj::mesh_t& m_Mesh;
    std::vector<XMFLOAT3>* m_Tangents;
    bool m_FlipTexcoordV;
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
        if ( meshContext->m_FlipTexcoordV )
        {
            fvTexcOut[ 1 ] = 1.f - fvTexcOut[ 1 ];
        }
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

static bool GenerateTangentVectorsForMesh( const tinyobj::attrib_t& attrib, const tinyobj::mesh_t& mesh, SMikkTSpaceContext* context, bool flipTexcoordV, std::vector<XMFLOAT3>* outTangents )
{
    outTangents->resize( mesh.num_face_vertices.size() * 3 );
    STinyObjMeshMikkTSpaceContext meshContext( attrib, mesh, outTangents, flipTexcoordV );
    context->m_pUserData = &meshContext;
    return genTangSpaceDefault( context );
}

static bool CreateMeshFromWavefrontOBJData( const tinyobj::attrib_t& attrib, const tinyobj::shape_t* shapes, uint32_t shapesCount, const SMeshProcessingParams& params, Mesh* outMesh )
{
    std::unordered_map<SVertexKey, uint32_t> vertexKeyToVertexIndexMap;

    size_t normalCount = attrib.normals.size() / 3;
    if ( normalCount == 0 )
        return false;

    bool hasMaterialOverride = params.m_MaterialIndexOverride != -1;

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
    if ( params.m_ApplyTransform )
    {
        vTransform = XMLoadFloat4x4( &params.m_Transform );
        XMVECTOR vDet;
        vNormalTransform = XMMatrixTranspose( XMMatrixInverse( &vDet, vTransform ) );
    }

    // For winding order selection
    const int originalTriangleIndices[ 3 ] = { 0, 1, 2 };
    const int changedTriangleIndices[ 3 ] = { 0, 2, 1 };
    const int* triangleIndices = params.m_ChangeWindingOrder ? changedTriangleIndices : originalTriangleIndices;

    for ( uint32_t iShapes = 0; iShapes < shapesCount; ++iShapes )
    {
        const tinyobj::mesh_t& mesh = shapes[ iShapes ].mesh;

        assert( mesh.num_face_vertices.size() == mesh.material_ids.size() );

        if ( !GenerateTangentVectorsForMesh( attrib, mesh, &mikkTSpaceContext, params.m_FlipTexcoordV, &tangents ) )
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
                outMesh->m_MaterialIds.push_back( materialId != -1 ? params.m_MaterialIndexBase + uint32_t( materialId ) : INVALID_MATERIAL_ID );
            }
            else
            {
                outMesh->m_MaterialIds.push_back( params.m_MaterialIndexOverride );
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
                    if ( outMesh->m_Vertices.size() == UINT_MAX )
                        return false;

                    vertexIndex = (uint32_t)outMesh->m_Vertices.size();
                    GPU::Vertex vertex;
                    vertex.position = XMFLOAT3( attrib.vertices[ idx.vertex_index * 3 ], attrib.vertices[ idx.vertex_index * 3 + 1 ], attrib.vertices[ idx.vertex_index * 3 + 2 ] );
                    vertex.normal = XMFLOAT3( attrib.normals[ idx.normal_index * 3 ], attrib.normals[ idx.normal_index * 3 + 1 ], attrib.normals[ idx.normal_index * 3 + 2 ] );
                    vertex.tangent = tangent;
                    vertex.texcoord = idx.texcoord_index != -1 ? XMFLOAT2( attrib.texcoords[ idx.texcoord_index * 2 ], attrib.texcoords[ idx.texcoord_index * 2 + 1 ] ) : XMFLOAT2( 0.0f, 0.0f );
                    if ( params.m_FlipTexcoordV )
                    {
                        vertex.texcoord.y = 1.f - vertex.texcoord.y;
                    }

                    if ( params.m_ApplyTransform )
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

                    outMesh->m_Vertices.emplace_back( vertex );

                    vertexKeyToVertexIndexMap.insert( std::make_pair( vertexKey, vertexIndex ) );
                }
                outMesh->m_Indices.push_back( vertexIndex );
            }
        }
    }

    return true;
}

struct STexture
{
    std::string m_Filename;
};

struct SMaterialTranslationContext
{
    explicit SMaterialTranslationContext( int32_t textureIndexBase ) : m_TextureIndexBase( textureIndexBase ) {}

    void TranslateMaterials( const tinyobj::material_t* srcMaterials, SMaterial* dstMaterials, uint32_t count );

    bool LoadTexturesFromFiles( const std::filesystem::path& parentFilenamePath, std::vector<CTexture>* textures );

    std::vector<STexture> m_Textures;
    std::unordered_map<std::string, int32_t> m_TextureFilenameToIndexMap;
    int32_t m_TextureIndexBase;
};

void SMaterialTranslationContext::TranslateMaterials( const tinyobj::material_t* srcMaterials, SMaterial* dstMaterials, uint32_t count )
{
    for ( uint32_t index = 0; index < count; ++index )
    {
        const tinyobj::material_t* srcMaterial = srcMaterials + index;
        SMaterial* dstMaterial = dstMaterials + index;
        dstMaterial->m_Albedo = DirectX::XMFLOAT3( srcMaterial->diffuse[ 0 ], srcMaterial->diffuse[ 1 ], srcMaterial->diffuse[ 2 ] );
        dstMaterial->m_Roughness = srcMaterial->roughness;
        dstMaterial->m_IOR = XMFLOAT3( std::clamp( srcMaterial->ior, 1.0f, MAX_MATERIAL_IOR ), 1.f, 1.f );
        dstMaterial->m_Opacity = srcMaterial->dissolve;
        dstMaterial->m_K = XMFLOAT3( 1.0f, 1.0f, 1.0f );
        dstMaterial->m_Tiling = XMFLOAT2( 1.0f, 1.0f );
        dstMaterial->m_MaterialType = EMaterialType::Plastic;
        dstMaterial->m_Multiscattering = false;
        dstMaterial->m_IsTwoSided = false;
        dstMaterial->m_HasRoughnessTexture = false;
        dstMaterial->m_Name = srcMaterial->name;

        int32_t albedoTextureIndex = INDEX_NONE;
        if ( srcMaterial->diffuse_texname.length() > 0 )
        {
            auto textureFilenameToIndexPairIt = m_TextureFilenameToIndexMap.find( srcMaterial->diffuse_texname );
            if ( textureFilenameToIndexPairIt != m_TextureFilenameToIndexMap.end() )
            {
                albedoTextureIndex = textureFilenameToIndexPairIt->second;
            }
            else
            {
                albedoTextureIndex = m_TextureIndexBase + (int32_t)m_Textures.size();
                m_TextureFilenameToIndexMap.insert( std::make_pair( srcMaterial->diffuse_texname, albedoTextureIndex ) );

                m_Textures.emplace_back();
                STexture& newTexture = m_Textures.back();
                newTexture.m_Filename = srcMaterial->diffuse_texname;
            }
        }
        dstMaterial->m_AlbedoTextureIndex = albedoTextureIndex;
    }
}

bool SMaterialTranslationContext::LoadTexturesFromFiles( const std::filesystem::path& parentFilenamePath, std::vector<CTexture>* textures )
{
    STextureCodec* codec = CTexture::CreateCodec();
    if ( codec == nullptr )
    {
        return false;
    }

    textures->reserve( textures->size() + m_Textures.size() );
    std::filesystem::path filenamePath;
    for ( const STexture& texture : m_Textures )
    {
        const std::string& filename = texture.m_Filename;
        filenamePath = filename;
        if ( filenamePath.is_relative() )
        {
            filenamePath = parentFilenamePath.parent_path() / filenamePath;
        }

        textures->emplace_back();
        CTexture& newTexture = textures->back();
        newTexture.Clear();
        newTexture.m_Name = std::move( texture.m_Filename );
        const std::string absoluteFilename = filenamePath.u8string();
        if ( !newTexture.LoadFromFile( absoluteFilename.c_str(), codec ) )
        {
            LOG_STRING_FORMAT( "Loading texture from file \"%s\" failed.\n", absoluteFilename.c_str() );
        }
    }

    CTexture::DestroyCodec( codec );
    return true;
}

bool Mesh::LoadFromWavefrontOBJFile( const std::filesystem::path& filenamePath, const SMeshProcessingParams& params, std::vector<SMaterial>* outMaterials, std::vector<CTexture>* outTextures )
{
    const std::string filename = filenamePath.u8string();
    const std::string MTLSearchPath = filenamePath.parent_path().u8string();
    LOG_STRING_FORMAT( "Loading mesh from: %s, MTL search path at: %s\n", filename.c_str(), MTLSearchPath.c_str() );

    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn;
    std::string err;
    bool loadSuccessful = tinyobj::LoadObj( &attrib, &shapes, &materials, &warn, &err, filename.c_str(), MTLSearchPath.c_str() );
    if ( !loadSuccessful )
    {
        return false;
    }

    if ( !CreateMeshFromWavefrontOBJData( attrib, shapes.data(), (uint32_t)shapes.size(), params, this ) )
    {
        return false;
    }

    bool hasMaterialOverride = params.m_MaterialIndexOverride != INVALID_MATERIAL_ID;
    if ( !hasMaterialOverride )
    {
        SMaterialTranslationContext translationContext( params.m_TextureIndexBase );
        const size_t originalSize = outMaterials->size();
        outMaterials->resize( originalSize + materials.size() );
        translationContext.TranslateMaterials( materials.data(), outMaterials->data() + originalSize, (uint32_t)materials.size() );
        translationContext.LoadTexturesFromFiles( filenamePath, outTextures );
    }

    return true;
}

bool CScene::LoadFromWavefrontOBJFile( const std::filesystem::path& filenamePath )
{
    const std::string filename = filenamePath.u8string();
    const std::string MTLSearchPath = filenamePath.parent_path().u8string();
    LOG_STRING_FORMAT( "Loading scene from: %s, MTL search path at: %s\n", filename.c_str(), MTLSearchPath.c_str() );

    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn;
    std::string err;
    bool loadSuccessful = tinyobj::LoadObj( &attrib, &shapes, &materials, &warn, &err, filename.c_str(), MTLSearchPath.c_str() );
    if ( !loadSuccessful )
    {
        return false;
    }

    m_Meshes.reserve( m_Meshes.size() + shapes.size() );
    m_InstanceTransforms.reserve( m_InstanceTransforms.size() + shapes.size() );
    m_Materials.reserve( m_Materials.size() + materials.size() );

    SMeshProcessingParams params;
    params.m_ApplyTransform = true;
    XMFLOAT4X4 RHS2LHSMatrix = MathHelper::s_IdentityMatrix4x4;
    RHS2LHSMatrix._11 = -1.0f;
    params.m_Transform = RHS2LHSMatrix;
    params.m_ChangeWindingOrder = true;
    params.m_FlipTexcoordV = true;
    params.m_MaterialIndexOverride = INVALID_MATERIAL_ID;

    for ( size_t shapeIndex = 0; shapeIndex < shapes.size(); ++shapeIndex )
    {
        m_Meshes.emplace_back();
        params.m_MaterialIndexBase = (uint32_t)m_Materials.size();
        Mesh& newMesh = m_Meshes.back();
        if ( !CreateMeshFromWavefrontOBJData( attrib, shapes.data() + shapeIndex, 1, params, &newMesh ) )
        {
            return false;
        }
        newMesh.m_Name = shapes[ shapeIndex ].name;

        m_InstanceTransforms.emplace_back( MathHelper::s_IdentityMatrix4x3 );
    }

    const bool hasMaterialOverride = params.m_MaterialIndexOverride != INVALID_MATERIAL_ID;
    if ( !hasMaterialOverride )
    {
        SMaterialTranslationContext translationContext( (int32_t)m_Textures.size() );
        const size_t originalSize = m_Materials.size();
        m_Materials.resize( originalSize + materials.size() );
        translationContext.TranslateMaterials( materials.data(), m_Materials.data() + originalSize, (uint32_t)materials.size() );
        translationContext.LoadTexturesFromFiles( filenamePath, &m_Textures );
    }

    return true;
}
