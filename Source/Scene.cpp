#include "stdafx.h"
#include "Scene.h"
#include "CommandLineArgs.h"
#include "Mesh.h"
#include "Logging.h"
#include "MessageBox.h"
#include "GPUBuffer.h"
#include "GPUTexture.h"
#include "Constants.h"
#include "../Shaders/LightSharedDef.inc.hlsl"
#include "imgui/imgui.h"

using namespace DirectX;

bool CScene::CreateMeshAndMaterialsFromWavefrontOBJFile( const char* filename, const char* MTLBaseDir, bool applyTransform, const DirectX::XMFLOAT4X4& transform, uint32_t materialIdOverride )
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn;
    std::string err;
    bool loadSuccessful = tinyobj::LoadObj( &attrib, &shapes, &materials, &warn, &err, filename, MTLBaseDir );
    if ( !loadSuccessful )
    {
        return false;
    }

    Mesh mesh;
    bool createMeshSuccessful = mesh.CreateFromWavefrontOBJData( attrib, shapes, (uint32_t)m_Materials.size(), applyTransform, transform, materialIdOverride );
    if ( !createMeshSuccessful )
    {
        return false;
    }

    m_Meshes.emplace_back( mesh );

    bool hasMaterialOverride = materialIdOverride != INVALID_MATERIAL_ID;
    if ( !hasMaterialOverride )
    {
        m_Materials.reserve( m_Materials.size() + materials.size() );
        m_MaterialNames.reserve( m_MaterialNames.size() + materials.size() );
        for ( auto& iterSrcMat : materials )
        {
            SMaterial dstMat;
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
    }

    return true;
}

bool CScene::LoadFromFile( const char* filepath )
{
    if ( filepath == nullptr || filepath[ 0 ] == '\0' )
        return false;

    size_t meshIndexBase = m_Meshes.size();

    if ( !LoadFromXMLFile( filepath ) )
        return false;

    {
        std::vector<uint32_t> reorderedTriangleIndices;
        for ( size_t iMesh = meshIndexBase; iMesh < m_Meshes.size(); ++iMesh )
        {
            Mesh& mesh = m_Meshes[ iMesh ];
            mesh.BuildBVH( nullptr, &reorderedTriangleIndices );

            uint32_t BVHMaxDepth = mesh.GetBVHMaxDepth();
            uint32_t BVHMaxStackSize = mesh.GetBVHMaxStackSize();
            LOG_STRING_FORMAT( "BLAS created from mesh %d. Node count:%d, max depth:%d, max stack size:%d\n", iMesh, mesh.GetBVHNodeCount(), BVHMaxDepth, BVHMaxStackSize );
        }
    }

    {
        assert( m_Meshes.size() == m_InstanceTransforms.size() );
        std::vector<BVHAccel::SInstance> BLASInstances;
        BLASInstances.reserve( m_Meshes.size() );
        for ( size_t iMesh = 0; iMesh < m_Meshes.size(); ++iMesh )
        {
            const Mesh& mesh = m_Meshes[ iMesh ];
            assert( mesh.GetBVHNodeCount() > 0 );
            const BVHAccel::BVHNode& BLASRoot = mesh.GetBVHNodes()[ 0 ];
            BLASInstances.emplace_back();
            BLASInstances.back().m_BoundingBox = BLASRoot.m_BoundingBox;
            BLASInstances.back().m_Transform = m_InstanceTransforms[ iMesh ];
        }

        std::vector<uint32_t> reorderedInstanceIndices;
        reorderedInstanceIndices.resize( m_InstanceTransforms.size() );
        uint32_t BVHMaxDepth = 0;
        uint32_t BVHMaxStackSize = 0;
        m_TLAS.clear();
        BVHAccel::BuildTLAS( BLASInstances.data(), reorderedInstanceIndices.data(), (uint32_t)m_Meshes.size(), &m_TLAS, &BVHMaxDepth, &BVHMaxStackSize );
        LOG_STRING_FORMAT( "TLAS created. Node count:%d, max depth:%d, max stack size:%d\n", m_TLAS.size(), BVHMaxDepth, BVHMaxStackSize );
    }

    uint32_t totalVertexCount = 0;
    uint32_t totalIndexCount = 0;
    uint32_t totalBVHNodeCount = (uint32_t)m_TLAS.size();
    for ( auto& mesh : m_Meshes )
    {
        totalVertexCount += mesh.GetVertexCount();
        totalIndexCount += mesh.GetIndexCount();
        totalBVHNodeCount += mesh.GetBVHNodeCount();
    }

    {
        std::vector<GPU::Vertex> vertices;
        vertices.resize( totalVertexCount );
        GPU::Vertex* dest = vertices.data();
        for ( auto& mesh : m_Meshes )
        {
            memcpy( dest, mesh.GetVertices().data(), sizeof( GPU::Vertex ) * mesh.GetVertexCount() );
            dest += mesh.GetVertexCount();
        }

        m_VerticesBuffer.reset( GPUBuffer::CreateStructured(
              sizeof( GPU::Vertex ) * totalVertexCount
            , sizeof( GPU::Vertex )
            , D3D11_USAGE_IMMUTABLE
            , D3D11_BIND_SHADER_RESOURCE
            , 0
            , vertices.data() ) );

        if ( !m_VerticesBuffer )
        {
            LOG_STRING( "Failed to create vertices buffer.\n" );
            return false;
        }
    }

    {
        std::vector<uint32_t> indices;
        indices.resize( totalIndexCount );
        uint32_t* dest = indices.data();
        uint32_t vertexOffset = 0;
        for ( auto& mesh : m_Meshes )
        {
            auto& meshIndices = mesh.GetIndices();
            for ( auto index : meshIndices )
            {
                ( *dest ) = index + vertexOffset;
                ++dest;
            }
            vertexOffset += mesh.GetVertexCount();
        }

        m_TrianglesBuffer.reset( GPUBuffer::CreateStructured(
              sizeof( uint32_t ) * totalIndexCount
            , sizeof( uint32_t )
            , D3D11_USAGE_IMMUTABLE
            , D3D11_BIND_SHADER_RESOURCE
            , 0
            , indices.data() ) );
        if ( !m_TrianglesBuffer )
        {
            LOG_STRING( "Failed to create triangles buffer.\n" );
            return false;
        }
    }

    {
        std::vector<GPU::BVHNode> BVHNodes;
        BVHNodes.resize( totalBVHNodeCount );

    }
    

    m_MaterialIdsBuffer.reset( GPUBuffer::CreateStructured(
          sizeof( uint32_t ) * m_Mesh.GetTriangleCount()
        , sizeof( uint32_t )
        , D3D11_USAGE_IMMUTABLE
        , D3D11_BIND_SHADER_RESOURCE
        , 0
        , m_Mesh.GetMaterialIds().data() ) );
    if ( !m_MaterialIdsBuffer )
    {
        CMessagebox::GetSingleton().Append( "Failed to create material id buffer.\n" );
        return false;
    }

    m_MaterialsBuffer.reset( GPUBuffer::CreateStructured(
          uint32_t( sizeof( GPU::Material ) * m_Mesh.GetMaterials().size() )
        , sizeof( GPU::Material )
        , D3D11_USAGE_DYNAMIC
        , D3D11_BIND_SHADER_RESOURCE
        , GPUResourceCreationFlags_CPUWriteable
        , m_Mesh.GetMaterials().data() ) );
    if ( !m_MaterialsBuffer )
    {
        CMessagebox::GetSingleton().Append( "Failed to create materials buffer.\n" );
        return false;
    }

    if ( !CommandLineArgs::Singleton()->GetNoBVHAccel() )
    {
        m_BVHNodesBuffer.reset( GPUBuffer::CreateStructured(
              sizeof( GPU::BVHNode ) * m_Mesh.GetBVHNodeCount()
            , sizeof( GPU::BVHNode )
            , D3D11_USAGE_IMMUTABLE
            , D3D11_BIND_SHADER_RESOURCE
            , 0
            , m_Mesh.GetBVHNodes() ) );
        if ( !m_BVHNodesBuffer )
        {
            CMessagebox::GetSingleton().Append( "Failed to create BVH nodes buffer.\n" );
            return false;
        }
    }

    m_LightsBuffer.reset( GPUBuffer::CreateStructured(
          sizeof( GPU::SLight ) * s_MaxLightsCount
        , sizeof( GPU::SLight )
        , D3D11_USAGE_DYNAMIC
        , D3D11_BIND_SHADER_RESOURCE
        , GPUResourceCreationFlags_CPUWriteable
        , nullptr ) );
    if ( !m_LightsBuffer )
    {
        CMessagebox::GetSingleton().Append( "Failed to create lights buffer.\n" );
        return false;
    }

    m_Camera.SetDirty();

    m_HasValidScene = true;
    m_ObjectSelection.DeselectAll();

    return true;
}

void CScene::Reset()
{
    m_MaxBounceCount = 2;
    m_FilmSize = XMFLOAT2( 0.05333f, 0.03f );
    m_FocalLength = 0.05f;
    m_FocalDistance = 2.0f;
    m_FilmDistanceNormalized = CalculateFilmDistanceNormalized();
    m_RelativeAperture = 8.0f; // initialize to f/8
    m_ApertureBladeCount = 7;
    m_ApertureRotation = 0.0f;
    m_ShutterTime = 1.0f;
    m_ISO = 100;
    m_BackgroundColor = { 0.0f, 0.0f, 0.0f, 0.f };
    m_FilterRadius = 1.0f;

    m_Mesh.Clear();
    m_Lights.clear();
    m_TriangleLights.clear();

    m_HasValidScene = false;
    m_ObjectSelection.DeselectAll();
}

bool CScene::LoadEnvironmentTextureFromFile( const wchar_t* filepath )
{
    m_EnvironmentTexture.reset( GPUTexture::CreateFromFile( filepath ) );
    return m_EnvironmentTexture.get() != nullptr;
}

void CScene::UpdateLightGPUData()
{
    if ( void* address = m_LightsBuffer->Map() )
    {
        GPU::SLight* GPULight = (GPU::SLight*)address;

        for ( uint32_t i = 0; i < (uint32_t)m_Lights.size(); ++i )
        {
            SLight* CPULight = m_Lights.data() + i;

            XMVECTOR xmPosition = XMLoadFloat3( &CPULight->position );
            XMVECTOR xmQuat = XMQuaternionRotationRollPitchYaw( CPULight->rotation.x, CPULight->rotation.y, CPULight->rotation.z );
            XMFLOAT3 size3 = XMFLOAT3( CPULight->size.x, CPULight->size.y, 1.0f );
            XMVECTOR xmScale = XMLoadFloat3( &size3 );
            XMMATRIX xmTransform = XMMatrixAffineTransformation( xmScale, g_XMZero, xmQuat, xmPosition );

            // Shader uses column major
            xmTransform = XMMatrixTranspose( xmTransform );
            XMFLOAT4X4 transform44;
            XMStoreFloat4x4( &transform44, xmTransform );
            GPULight->transform = XMFLOAT4X3( (float*)&transform44 );

            GPULight->color = CPULight->color;

            switch ( CPULight->lightType )
            {
            case ELightType::Point:
            {
                GPULight->flags = LIGHT_FLAGS_POINT_LIGHT;
                break;
            }
            case ELightType::Rectangle:
            {
                GPULight->flags = 0;
                break;
            }
            default:
            {
                GPULight->flags = 0;
                break;
            }
            }

            ++GPULight;
        }

        for ( uint32_t i = 0; i < (uint32_t)m_TriangleLights.size(); ++i )
        {
            const STriangleLight* CPUlight = m_TriangleLights.data() + i;
            GPULight->color = CPUlight->m_Radiance;
            GPULight->primitiveId = CPUlight->m_TriangleId;
            GPULight->invSurfaceArea = CPUlight->m_InvSurfaceArea;
            GPULight->flags = LIGHT_FLAGS_TRIANGLE_LIGHT;
            ++GPULight;
        }

        m_LightsBuffer->Unmap();
    }
}

void CScene::UpdateMaterialGPUData()
{
    if ( void* address = m_MaterialsBuffer->Map() )
    {
        auto materials = m_Mesh.GetMaterials();
        for ( uint32_t i = 0; i < (uint32_t)materials.size(); ++i )
        {
            SMaterial* materialSetting = materials.data() + i;
            GPU::Material* material = ( (GPU::Material*)address ) + i;
            material->albedo = materialSetting->m_Albedo;
            material->emission = materialSetting->m_Emission;
            material->ior = materialSetting->m_IOR;
            material->k = materialSetting->m_K;
            material->roughness = std::clamp( materialSetting->m_Roughness, 0.0f, 1.0f );
            material->texTiling = materialSetting->m_Tiling;
            material->transmission = materialSetting->m_IsMetal ? 0.0f : materialSetting->m_Transmission;
            material->flags = materialSetting->m_IsMetal ? MATERIAL_FLAG_IS_METAL : 0;
            material->flags |= materialSetting->m_HasAlbedoTexture ? MATERIAL_FLAG_ALBEDO_TEXTURE : 0;
            material->flags |= materialSetting->m_HasEmissionTexture ? MATERIAL_FLAG_EMISSION_TEXTURE : 0;
            material->flags |= materialSetting->m_HasRoughnessTexture ? MATERIAL_FLAG_ROUGHNESS_TEXTURE : 0;
        }
        m_MaterialsBuffer->Unmap();
    }
}

float CScene::GetFilmDistance() const
{
    return m_FilmDistanceNormalized * m_FocalLength;
}

// Calculate focal distance from focal length and film distance.
// Based on the Gaussian lens equation.
float CScene::CalculateFocalDistance() const
{
    float filmDistance = GetFilmDistance();
    float denom = std::fmaxf( 0.0f, m_FocalLength - filmDistance );
    return std::fminf( s_MaxFocalDistance, ( m_FocalLength * filmDistance ) / denom ); // Clamp this value before it gets too large and make ray generation output invalid numbers.
}

// Calculate film distance from focal length and focal distance.
// Based on the Gaussian lens equation.
float CScene::CalculateFilmDistance() const
{
    return ( m_FocalLength * m_FocalDistance ) / ( m_FocalLength + m_FocalDistance );
}

float CScene::CalculateFilmDistanceNormalized() const
{
    return CalculateFilmDistance() / m_FocalLength;
}

