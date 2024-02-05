#include "stdafx.h"
#include "Scene.h"
#include "CommandLineArgs.h"
#include "Mesh.h"
#include "Logging.h"
#include "MessageBox.h"
#include "GPUBuffer.h"
#include "GPUTexture.h"
#include "Constants.h"
#include "StringConversion.h"
#include "MathHelper.h"
#include "../Shaders/LightSharedDef.inc.hlsl"
#include "imgui/imgui.h"

using namespace DirectX;

bool CScene::CreateMeshAndMaterialsFromWavefrontOBJFile( const char* filename, const char* MTLBaseDir, bool applyTransform, const DirectX::XMFLOAT4X4& transform, bool changeWindingOrder, uint32_t materialIdOverride )
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
    bool createMeshSuccessful = mesh.CreateFromWavefrontOBJData( attrib, shapes, (uint32_t)m_Materials.size(), applyTransform, transform, changeWindingOrder, materialIdOverride );
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
            SMaterial destMaterial;
            destMaterial.m_Albedo = DirectX::XMFLOAT3( iterSrcMat.diffuse[ 0 ], iterSrcMat.diffuse[ 1 ], iterSrcMat.diffuse[ 2 ] );
            destMaterial.m_Roughness = iterSrcMat.roughness;
            destMaterial.m_IOR = XMFLOAT3( std::clamp( iterSrcMat.ior, 1.0f, MAX_MATERIAL_IOR ), 1.f, 1.f );
            destMaterial.m_K = XMFLOAT3( 1.0f, 1.0f, 1.0f );
            destMaterial.m_Tiling = XMFLOAT2( 1.0f, 1.0f );
            destMaterial.m_MaterialType = EMaterialType::Plastic;
            destMaterial.m_Multiscattering = false;
            destMaterial.m_IsTwoSided = false;
            destMaterial.m_HasAlbedoTexture = false;
            destMaterial.m_HasRoughnessTexture = false;
            m_Materials.emplace_back( destMaterial );
            m_MaterialNames.emplace_back( iterSrcMat.name );
        }
    }

    return true;
}

static void GetDefaultMaterial(SMaterial* material)
{
    material->m_Albedo = XMFLOAT3( 1.f, 0.f, 1.f );
    material->m_Roughness = 1.f;
    material->m_IOR = XMFLOAT3( 1.f, 1.f, 1.f );
    material->m_K = XMFLOAT3( 1.0f, 1.0f, 1.0f );
    material->m_Tiling = XMFLOAT2( 1.0f, 1.0f );
    material->m_MaterialType = EMaterialType::Diffuse;
    material->m_Multiscattering = false;
    material->m_HasAlbedoTexture = false;
    material->m_HasRoughnessTexture = false;
}

bool CScene::LoadFromFile( const std::filesystem::path& filepath )
{
    if ( !filepath.has_filename() )
        return false;

    size_t meshIndexBase = m_Meshes.size();

    {
        const std::filesystem::path extension = filepath.extension();
        bool isWavefrontOBJFile = true;
        if ( extension == ".xml" || extension == ".XML" )
        {
            isWavefrontOBJFile = false;
        }

        bool loadResult = isWavefrontOBJFile ? LoadFromWavefrontOBJFile( filepath ) : LoadFromXMLFile( filepath );
        if ( !loadResult )
        { 
            return false;
        }
    }

    // Assign default material
    {
        for ( size_t iMesh = meshIndexBase; iMesh < m_Meshes.size(); ++iMesh )
        {
            uint32_t defaultMaterialIndex = INVALID_MATERIAL_ID;

            auto& materialIndices = m_Meshes[ iMesh ].GetMaterialIds();
            for ( auto& materialIndex : materialIndices )
            {
                if ( materialIndex == INVALID_MATERIAL_ID )
                {
                    if ( defaultMaterialIndex == INVALID_MATERIAL_ID )
                    { 
                        if ( m_Materials.size() < (size_t)std::numeric_limits<uint32_t>::max() )
                        { 
                            defaultMaterialIndex = (uint32_t)m_Materials.size();
                        }
                        else
                        {
                            LOG_STRING_FORMAT( "Trying to create default material %s but material count has exceeded limit.", m_Meshes[ iMesh ].GetName().c_str() );
                            return false;
                        }
                    }
                    materialIndex = defaultMaterialIndex;
                }
            }

            if ( defaultMaterialIndex != INVALID_MATERIAL_ID )
            {
                SMaterial material;
                GetDefaultMaterial( &material );
                m_Materials.emplace_back( material );
                m_MaterialNames.emplace_back( m_Meshes[ iMesh ].GetName() );
            }
        }
    }

    {
        std::vector<uint32_t> reorderedTriangleIndices;
        for ( size_t iMesh = meshIndexBase; iMesh < m_Meshes.size(); ++iMesh )
        {
            Mesh& mesh = m_Meshes[ iMesh ];
            mesh.BuildBVH( &reorderedTriangleIndices );

            uint32_t BVHMaxDepth = mesh.GetBVHMaxDepth();
            LOG_STRING_FORMAT( "BLAS created from mesh %s. Node count:%d, depth:%d\n", mesh.GetName().c_str(), mesh.GetBVHNodeCount(), BVHMaxDepth );
        }
    }

    m_TLAS.clear();
    {
        assert( m_Meshes.size() == m_InstanceTransforms.size() ); // For now, mesh count must equal to instance count
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

        std::vector<uint32_t> instanceDepths; // Which depth of the TLAS each instance is at
        instanceDepths.resize( m_InstanceTransforms.size() );
        m_ReorderedInstanceIndices.resize( m_InstanceTransforms.size() );
        uint32_t BVHMaxDepth = 0;
        uint32_t BVHMaxStackSize = 0;
        BVHAccel::BuildTLAS( BLASInstances.data(), m_ReorderedInstanceIndices.data(), (uint32_t)m_Meshes.size(), &m_TLAS, &BVHMaxDepth, &BVHMaxStackSize, instanceDepths.data() );
        LOG_STRING_FORMAT( "TLAS created. Node count:%d, depth:%d\n", m_TLAS.size(), BVHMaxDepth );

        uint32_t maxStackSize = 0;
        for ( uint32_t iInstance = 0; iInstance < (uint32_t)instanceDepths.size(); ++iInstance )
        {
            uint32_t originalInstance = m_ReorderedInstanceIndices[ iInstance ];
            maxStackSize = std::max( maxStackSize, instanceDepths[ iInstance ] + m_Meshes[ originalInstance ].GetBVHMaxDepth() );
        }
        m_BVHTraversalStackSize = maxStackSize;
        LOG_STRING_FORMAT( "BVH traversal stack size requirement is %d\n", maxStackSize );
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

    LOG_STRING_FORMAT( "Total vertex count %d, total index count %d, total BVH node count %d\n", totalVertexCount, totalIndexCount, totalBVHNodeCount );

    if ( CommandLineArgs::Singleton()->GetOutputBVHToFile() )
    {
        std::filesystem::path baseDirectory = filepath;
        baseDirectory.remove_filename();
        std::string baseDirectoryU8String = baseDirectory.u8string();

        char formattedFilenameBuffer[ MAX_PATH ];
        uint32_t instanceIndex = 0;
        for ( auto& index : m_ReorderedInstanceIndices )
        {
            const Mesh& mesh = m_Meshes[ index ];
            sprintf_s( formattedFilenameBuffer, ARRAY_LENGTH( formattedFilenameBuffer ), "%s/BLAS_Instance_%d_%s.xml", baseDirectoryU8String.c_str(), instanceIndex, mesh.GetName().c_str() );
            FILE* file = fopen( formattedFilenameBuffer, "w" );
            if ( file )
            {
                BVHAccel::SerializeBVHToXML( mesh.GetBVHNodes(), file );
                fclose( file );
                LOG_STRING_FORMAT( "BLAS written to file %s\n", formattedFilenameBuffer );
            }
            ++instanceIndex;
        }

        sprintf_s( formattedFilenameBuffer, ARRAY_LENGTH( formattedFilenameBuffer ), "%s/TLAS.xml", baseDirectoryU8String.c_str() );
        FILE* file = fopen( formattedFilenameBuffer, "w" );
        if ( file )
        {
            BVHAccel::SerializeBVHToXML( m_TLAS.data(), file );
            fclose( file );
            LOG_STRING_FORMAT( "TLAS written to file %s\n", formattedFilenameBuffer );
        }
    }

    const uint32_t s_MaxBVHNodeCount = 2147483647;
    if ( totalBVHNodeCount > s_MaxBVHNodeCount )
    {
        LOG_STRING_FORMAT( "Total BVH node count %d exceeds limit %d\n", totalBVHNodeCount, s_MaxBVHNodeCount );
        return false;
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

        if ( m_VerticesBuffer )
        {
            LOG_STRING_FORMAT( "Vertex buffer created, size %d\n", sizeof( GPU::Vertex ) * totalVertexCount );
        }
        else 
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

        if ( m_TrianglesBuffer )
        {
            LOG_STRING_FORMAT( "Triangle buffer created, size %d\n", sizeof( uint32_t )* totalIndexCount );
        }
        else 
        {
            LOG_STRING( "Failed to create triangles buffer.\n" );
            return false;
        }
    }

    {
        std::vector<GPU::BVHNode> BVHNodes;
        BVHNodes.resize( totalBVHNodeCount );

        std::vector<uint32_t> BLASNodeIndexOffsets;
        BLASNodeIndexOffsets.reserve( m_Meshes.size() );

        GPU::BVHNode* dest = BVHNodes.data() + m_TLAS.size();
        uint32_t triangleIndexOffset = 0;
        uint32_t nodeIndexOffset = (uint32_t)m_TLAS.size();
        for ( auto& mesh : m_Meshes )
        {
            BVHAccel::PackBVH( mesh.GetBVHNodes(), mesh.GetBVHNodeCount(), true, dest, nodeIndexOffset, triangleIndexOffset );
            dest += mesh.GetBVHNodeCount();
            BLASNodeIndexOffsets.push_back( nodeIndexOffset );
            triangleIndexOffset += mesh.GetTriangleCount();;
            nodeIndexOffset += mesh.GetBVHNodeCount();
        }

        // Make the TLAS point to the BLASes
        std::vector<BVHAccel::BVHNode> TLAS = m_TLAS; // we make a copy here so the original TLAS could be used for CPU ray trace
        for ( auto& node : TLAS )
        {
            if ( node.m_PrimCount > 0 )
            {
                uint32_t primIndex = node.m_PrimIndex;
                assert( node.m_PrimCount == 1 );
                uint32_t originalPrimIndex = m_ReorderedInstanceIndices[ primIndex ];
                node.m_ChildIndex = BLASNodeIndexOffsets[ originalPrimIndex ];
                node.m_InstanceIndex = primIndex;
            }
        }

        dest = BVHNodes.data();
        BVHAccel::PackBVH( TLAS.data(), (uint32_t)TLAS.size(), false, dest );

        m_BVHNodesBuffer.reset( GPUBuffer::CreateStructured(
              sizeof( GPU::BVHNode ) * totalBVHNodeCount
            , sizeof( GPU::BVHNode )
            , D3D11_USAGE_IMMUTABLE
            , D3D11_BIND_SHADER_RESOURCE
            , 0
            , BVHNodes.data() ) );

        if ( m_BVHNodesBuffer )
        {
            LOG_STRING_FORMAT( "BVH node buffer created, size %d\n", sizeof( GPU::BVHNode )* totalBVHNodeCount );
        }
        else
        {
            LOG_STRING( "Failed to create BVH nodes buffer.\n" );
            return false;
        }
    }
    
    {
        std::vector<uint32_t> materialIds;
        materialIds.resize( totalIndexCount / 3 );
        uint32_t* dest = materialIds.data();
        for ( auto& mesh : m_Meshes )
        {
            assert( mesh.GetMaterialIds().size() == mesh.GetIndexCount() / 3 );
            memcpy( dest, mesh.GetMaterialIds().data(), mesh.GetMaterialIds().size() * sizeof( uint32_t ) );
            dest += mesh.GetMaterialIds().size();
        }

        m_MaterialIdsBuffer.reset( GPUBuffer::CreateStructured(
              sizeof( uint32_t ) * (uint32_t)materialIds.size()
            , sizeof( uint32_t )
            , D3D11_USAGE_IMMUTABLE
            , D3D11_BIND_SHADER_RESOURCE
            , 0
            , materialIds.data() ) );

        if ( m_MaterialIdsBuffer )
        {
            LOG_STRING_FORMAT( "Material id buffer created, size %d\n", sizeof( uint32_t ) * materialIds.size() );
        }
        else 
        {
            LOG_STRING( "Failed to create material id buffer.\n" );
            return false;
        }
    }

    {
        std::vector<DirectX::XMFLOAT4X3> instanceTransforms;
        instanceTransforms.resize( m_InstanceTransforms.size() * 2 );

        DirectX::XMFLOAT4X3* dest = instanceTransforms.data();
        for ( uint32_t i = 0; i < (uint32_t)m_InstanceTransforms.size(); ++i )
        {
            const DirectX::XMFLOAT4X3& transform = m_InstanceTransforms[ m_ReorderedInstanceIndices[ i ] ];
            *dest = DirectX::XMFLOAT4X3( transform._11, transform._21, transform._31, transform._41, transform._12, transform._22, transform._32, transform._42, transform._13, transform._23, transform._33, transform._43 );
            ++dest;
        }

        DirectX::XMFLOAT4X3 rowMajorMatrix;
        for ( uint32_t i = 0; i < (uint32_t)m_InstanceTransforms.size(); ++i )
        {
            const DirectX::XMFLOAT4X3& transform = m_InstanceTransforms[ m_ReorderedInstanceIndices[ i ] ];
            DirectX::XMMATRIX vMatrix = DirectX::XMLoadFloat4x3( &transform );
            DirectX::XMVECTOR vDet;
            vMatrix = DirectX::XMMatrixInverse( &vDet, vMatrix );
            DirectX::XMStoreFloat4x3( &rowMajorMatrix, vMatrix );
            *dest = DirectX::XMFLOAT4X3( rowMajorMatrix._11, rowMajorMatrix._21, rowMajorMatrix._31, rowMajorMatrix._41, rowMajorMatrix._12, rowMajorMatrix._22, rowMajorMatrix._32, rowMajorMatrix._42, rowMajorMatrix._13, rowMajorMatrix._23, rowMajorMatrix._33, rowMajorMatrix._43 );
            ++dest;
        }

        m_InstanceTransformsBuffer.reset( GPUBuffer::CreateStructured(
              sizeof( DirectX::XMFLOAT4X3 ) * (uint32_t)instanceTransforms.size()
            , sizeof( DirectX::XMFLOAT4X3 )
            , D3D11_USAGE_IMMUTABLE
            , D3D11_BIND_SHADER_RESOURCE
            , 0
            , instanceTransforms.data() ) );

        if ( m_InstanceTransformsBuffer )
        {
            LOG_STRING_FORMAT( "Instance transform buffer created, size %d\n", sizeof( DirectX::XMFLOAT4X3 ) * instanceTransforms.size() );
        }
        else 
        {
            LOG_STRING( "Failed to create instance transform buffer.\n" );
            return false;
        }
    }

    {
        std::vector<uint32_t> instanceLightIndices; // Instance to light index mapping
        instanceLightIndices.resize( m_InstanceTransforms.size(), LIGHT_INDEX_INVALID );

        // Update mesh light instance indices since they are reordered by TLAS
        uint32_t lightIndex = 0;
        for ( auto& light : m_MeshLights )
        {
            uint32_t originalInstanceIndex = light.m_InstanceIndex;
            uint32_t newInstanceIndex = (uint32_t)std::distance( m_ReorderedInstanceIndices.begin(), std::find( m_ReorderedInstanceIndices.begin(), m_ReorderedInstanceIndices.end(), originalInstanceIndex ) );
            light.m_InstanceIndex = newInstanceIndex;

            instanceLightIndices[ newInstanceIndex ] = lightIndex;
            ++lightIndex;
        }

        // Create instance light indices buffer
        m_InstanceLightIndicesBuffer.reset( GPUBuffer::Create( 
              sizeof( uint32_t ) * (uint32_t)m_InstanceTransforms.size()
            , sizeof( uint32_t )
            , DXGI_FORMAT_R32_UINT
            , D3D11_USAGE_IMMUTABLE
            , D3D11_BIND_SHADER_RESOURCE
            , 0
            , instanceLightIndices.data() ) );

        if ( m_InstanceLightIndicesBuffer )
        {
            LOG_STRING_FORMAT( "Instance light indices buffer created, size %d\n", sizeof( uint32_t ) * m_InstanceTransforms.size() );
        }
        else 
        {
            LOG_STRING( "Failed to create instance light indices buffer.\n" );
            return false;
        }
    }
    
    m_MaterialsBuffer.reset( GPUBuffer::CreateStructured(
          uint32_t( sizeof( GPU::Material ) * m_Materials.size() )
        , sizeof( GPU::Material )
        , D3D11_USAGE_DYNAMIC
        , D3D11_BIND_SHADER_RESOURCE
        , GPUResourceCreationFlags_CPUWriteable ) );

    if ( m_MaterialsBuffer )
    {
        LOG_STRING_FORMAT( "Material buffer created, size %d\n", sizeof( GPU::Material )* m_Materials.size() );
    }
    else 
    {
        LOG_STRING( "Failed to create materials buffer.\n" );
        return false;
    }

    m_LightsBuffer.reset( GPUBuffer::CreateStructured(
          sizeof( GPU::SLight ) * s_MaxLightsCount
        , sizeof( GPU::SLight )
        , D3D11_USAGE_DYNAMIC
        , D3D11_BIND_SHADER_RESOURCE
        , GPUResourceCreationFlags_CPUWriteable
        , nullptr ) );

    if ( m_LightsBuffer )
    {
        LOG_STRING_FORMAT( "Light buffer created, size %d\n", sizeof( GPU::SLight )* s_MaxLightsCount );
    }
    else 
    {
        LOG_STRING( "Failed to create lights buffer.\n" );
        return false;
    }

    m_FilmTexture.reset( GPUTexture::Create(
          m_ResolutionWidth
        , m_ResolutionHeight
        , DXGI_FORMAT_R32G32B32A32_FLOAT
        , GPUResourceCreationFlags_HasUAV | GPUResourceCreationFlags_IsRenderTarget ) );
    if ( !m_FilmTexture )
    {
        return false;
    }

    m_SamplePositionTexture.reset( GPUTexture::Create(
          m_ResolutionWidth
        , m_ResolutionHeight
        , DXGI_FORMAT_R32G32_FLOAT
        , GPUResourceCreationFlags_HasUAV ) );
    if ( !m_SamplePositionTexture )
    { 
        return false;
    }

    m_SampleValueTexture.reset( GPUTexture::Create(
          m_ResolutionWidth
        , m_ResolutionHeight
        , DXGI_FORMAT_R32G32B32A32_FLOAT
        , GPUResourceCreationFlags_HasUAV ) );
    if ( !m_SampleValueTexture )
    {
        return false;
    }

    m_RenderResultTexture.reset( GPUTexture::Create( 
          m_ResolutionWidth
        , m_ResolutionHeight
        , DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
        , GPUResourceCreationFlags_IsRenderTarget ) );
    if ( !m_RenderResultTexture )
    { 
        return false;
    }

    m_Camera.SetDirty();

    m_HasValidScene = true;
    m_ObjectSelection.DeselectAll();

    return true;
}

void CScene::Reset()
{
    m_ResolutionWidth = CommandLineArgs::Singleton()->ResolutionX();
    m_ResolutionHeight = CommandLineArgs::Singleton()->ResolutionY();
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
    m_FilterRadius = 1.0f;

    m_Meshes.clear();
    m_EnvironmentLight.reset();
    m_PunctualLights.clear();
    m_MeshLights.clear();
    m_Materials.clear();
    m_MaterialNames.clear();
    m_TLAS.clear();
    m_ReorderedInstanceIndices.clear();
    m_InstanceTransforms.clear();

    m_HasValidScene = false;
    m_ObjectSelection.DeselectAll();
}

void CScene::UpdateLightGPUData()
{
    if ( void* address = m_LightsBuffer->Map() )
    {
        GPU::SLight* GPULight = (GPU::SLight*)address;

        // todo: cache the offsets somewhere so it does not need to be calculated every time
        std::vector<uint32_t> meshTriangleOffsets;
        meshTriangleOffsets.reserve( m_Meshes.size() );
        uint32_t triangleCount = 0;
        for ( auto& mesh : m_Meshes )
        {
            meshTriangleOffsets.emplace_back( triangleCount );
            triangleCount += mesh.GetTriangleCount();
        }
        
        for ( uint32_t i = 0; i < (uint32_t)m_MeshLights.size(); ++i )
        {
            SMeshLight* CPULight = m_MeshLights.data() + i;

            GPULight->radiance = CPULight->color;
            uint32_t originalInstanceIndex = m_ReorderedInstanceIndices[ CPULight->m_InstanceIndex ];
            GPULight->position_or_triangleRange.x = *(float*)&meshTriangleOffsets[ originalInstanceIndex ];
            uint32_t triangleCount = m_Meshes[ originalInstanceIndex ].GetTriangleCount();
            GPULight->position_or_triangleRange.y = *(float*)&triangleCount;
            GPULight->position_or_triangleRange.z = *(float*)&CPULight->m_InstanceIndex;
            GPULight->flags = LIGHT_FLAGS_MESH_LIGHT;

            ++GPULight;
        }

        if ( m_EnvironmentLight )
        {
            SEnvironmentLight* CPULight = m_EnvironmentLight.get();
            GPULight->radiance = CPULight->m_Color;
            GPULight->flags = LIGHT_FLAGS_ENVIRONMENT_LIGHT;

            ++GPULight;
        }

        for ( uint32_t i = 0; i < (uint32_t)m_PunctualLights.size(); ++i )
        {
            SPunctualLight* CPULight = m_PunctualLights.data() + i;

            GPULight->radiance = CPULight->m_Color;
            GPULight->position_or_triangleRange = CPULight->m_IsDirectionalLight ? CPULight->CalculateDirection() : CPULight->m_Position;
            GPULight->flags = CPULight->m_IsDirectionalLight ? LIGHT_FLAGS_DIRECTIONAL_LIGHT : LIGHT_FLAGS_POINT_LIGHT;    

            ++GPULight;
        }

        m_LightsBuffer->Unmap();
    }
}

static uint32_t TranslateToMaterialType( EMaterialType materialType )
{
    return (uint32_t)materialType;
}

void CScene::UpdateMaterialGPUData()
{
    if ( void* address = m_MaterialsBuffer->Map() )
    {
        for ( uint32_t i = 0; i < (uint32_t)m_Materials.size(); ++i )
        {
            SMaterial* materialSetting = m_Materials.data() + i;
            GPU::Material* material = ( (GPU::Material*)address ) + i;
            material->albedo = materialSetting->m_MaterialType == EMaterialType::Conductor ? materialSetting->m_K : materialSetting->m_Albedo;
            material->ior = materialSetting->m_IOR;
            material->roughness = std::clamp( materialSetting->m_Roughness, 0.0f, 1.0f );
            material->texTiling = materialSetting->m_Tiling;
            material->flags = TranslateToMaterialType( materialSetting->m_MaterialType ) & MATERIAL_FLAG_TYPE_MASK;
            material->flags |= materialSetting->m_Multiscattering ? MATERIAL_FLAG_MULTISCATTERING : 0;
            material->flags |= materialSetting->m_IsTwoSided ? MATERIAL_FLAG_IS_TWOSIDED : 0;
            material->flags |= materialSetting->m_HasAlbedoTexture ? MATERIAL_FLAG_ALBEDO_TEXTURE : 0;
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

bool SEnvironmentLight::CreateTextureFromFile()
{
    std::wstring filename = StringConversion::UTF8StringToUTF16WString( m_TextureFileName );
    m_Texture.reset( GPUTexture::CreateFromFile( filename.c_str() ) );
    return m_Texture.get() != nullptr;
}

void SPunctualLight::SetEulerAnglesFromDirection( const DirectX::XMFLOAT3& scalarDirection )
{
    XMVECTOR initialDirection = g_XMIdentityR0;
    XMVECTOR direction = XMLoadFloat3( &scalarDirection );
    XMVECTOR axis = XMVector3Cross( initialDirection, direction );
    XMVECTOR axisLength = XMVector3Length( axis );
    float scalarAxisLength;
    XMStoreFloat( &scalarAxisLength, axisLength );
    XMVECTOR dot = XMVector3Dot( direction, initialDirection );
    float scalarDot;
    XMStoreFloat( &scalarDot, dot );
    if ( scalarAxisLength < 1e-7f )
    {
        if ( scalarDot >= 0.f )
        {
            m_EulerAngles = XMFLOAT3( 0.f, 0.f, 0.f );
        }
        else
        {
            m_EulerAngles = XMFLOAT3( 0.f, (float)M_PI, 0.f );
        }
    }
    else
    {
        axis = XMVectorDivide( axis, axisLength );
        float scalarAngle = (float)acos( scalarDot );
        XMMATRIX rotationMatrix = XMMatrixRotationAxis( axis, scalarAngle );
        XMFLOAT3X3 scalarRotationMatrix;
        XMStoreFloat3x3( &scalarRotationMatrix, rotationMatrix );
        m_EulerAngles = MathHelper::MatrixRotationToRollPitchYall( scalarRotationMatrix );
    }
}

DirectX::XMFLOAT3 SPunctualLight::CalculateDirection() const
{
    XMVECTOR rollPitchYall = XMLoadFloat3( &m_EulerAngles );
    XMMATRIX matrix = XMMatrixRotationRollPitchYawFromVector( rollPitchYall );
    XMVECTOR initialDirection = g_XMIdentityR0;
    XMVECTOR direction = XMVector3Transform( initialDirection, matrix );
    XMFLOAT3 scalarDirection;
    XMStoreFloat3( &scalarDirection, direction );
    return scalarDirection;
}

