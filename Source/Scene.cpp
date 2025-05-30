#include "stdafx.h"
#include "Scene.h"
#include "CommandLineArgs.h"
#include "Mesh.h"
#include "Logging.h"
#include "MessageBox.h"
#include "GPUBuffer.h"
#include "GPUTexture.h"
#include "Constants.h"
#include "D3D12Adapter.h"
#include "D3D12GPUDescriptorHeap.h"
#include "StringConversion.h"
#include "MathHelper.h"
#include "../Shaders/LightSharedDef.inc.hlsl"
#include "imgui/imgui.h"

using namespace DirectX;

static DXGI_FORMAT GetDXGIFormat( ETexturePixelFormat pixelFormat )
{
    switch ( pixelFormat )
    {
    case ETexturePixelFormat::R8G8B8A8_sRGB:
    {
        return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    }
    default:
    {
        return DXGI_FORMAT_UNKNOWN;
    }
    }
    return DXGI_FORMAT_UNKNOWN;
}

static void GetDefaultMaterial( SMaterial* material )
{
    material->m_Albedo = XMFLOAT3( 1.f, 0.f, 1.f );
    material->m_Roughness = 1.f;
    material->m_IOR = XMFLOAT3( 1.f, 1.f, 1.f );
    material->m_K = XMFLOAT3( 1.0f, 1.0f, 1.0f );
    material->m_Tiling = XMFLOAT2( 1.0f, 1.0f );
    material->m_MaterialType = EMaterialType::Diffuse;
    material->m_AlbedoTextureIndex = INDEX_NONE;
    material->m_Multiscattering = false;
    material->m_IsTwoSided = false;
    material->m_HasRoughnessTexture = false;
    material->m_Name = "DefaultMaterial";
}

bool CScene::LoadFromFile( const std::filesystem::path& filepath )
{
    if ( !filepath.has_filename() )
        return false;

    const size_t meshIndexBase = m_Meshes.size();
    const size_t textureIndexBase = m_Textures.size();

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
        m_OriginalInstanceIndices.resize( m_InstanceTransforms.size() );
        uint32_t BVHMaxDepth = 0;
        uint32_t BVHMaxStackSize = 0;
        BVHAccel::BuildTLAS( BLASInstances.data(), m_OriginalInstanceIndices.data(), (uint32_t)m_Meshes.size(), &m_TLAS, &BVHMaxDepth, &BVHMaxStackSize, instanceDepths.data() );
        LOG_STRING_FORMAT( "TLAS created. Node count:%d, depth:%d\n", m_TLAS.size(), BVHMaxDepth );

        uint32_t maxStackSize = 0;
        for ( uint32_t iInstance = 0; iInstance < (uint32_t)instanceDepths.size(); ++iInstance )
        {
            uint32_t originalInstance = m_OriginalInstanceIndices[ iInstance ];
            maxStackSize = std::max( maxStackSize, instanceDepths[ iInstance ] + m_Meshes[ originalInstance ].GetBVHMaxDepth() );
        }
        m_BVHTraversalStackSize = maxStackSize;
        LOG_STRING_FORMAT( "BVH traversal stack size requirement is %d\n", maxStackSize );

        m_ReorderedInstanceIndices.resize( m_OriginalInstanceIndices.size() );
        for ( uint32_t originalInstanceIndex : m_OriginalInstanceIndices )
        {
            const uint32_t reorderedInstanceIndex = (uint32_t)std::distance( m_OriginalInstanceIndices.begin(), std::find( m_OriginalInstanceIndices.begin(), m_OriginalInstanceIndices.end(), originalInstanceIndex ) );
            m_ReorderedInstanceIndices[ originalInstanceIndex ] = reorderedInstanceIndex;
        }
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
        for ( auto& index : m_OriginalInstanceIndices )
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

        m_VerticesBuffer.Reset( GPUBuffer::CreateStructured(
              sizeof( GPU::Vertex ) * totalVertexCount
            , sizeof( GPU::Vertex )
            , EGPUBufferUsage::Default
            , EGPUBufferBindFlag_ShaderResource
            , vertices.data()
            , D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE ) );

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

        m_TrianglesBuffer.Reset( GPUBuffer::CreateStructured(
              sizeof( uint32_t ) * totalIndexCount
            , sizeof( uint32_t )
            , EGPUBufferUsage::Default
            , EGPUBufferBindFlag_ShaderResource
            , indices.data()
            , D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE ) );

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
                uint32_t originalPrimIndex = m_OriginalInstanceIndices[ primIndex ];
                node.m_ChildIndex = BLASNodeIndexOffsets[ originalPrimIndex ];
                node.m_InstanceIndex = primIndex;
            }
        }

        dest = BVHNodes.data();
        BVHAccel::PackBVH( TLAS.data(), (uint32_t)TLAS.size(), false, dest );

        m_BVHNodesBuffer.Reset( GPUBuffer::CreateStructured(
              sizeof( GPU::BVHNode ) * totalBVHNodeCount
            , sizeof( GPU::BVHNode )
            , EGPUBufferUsage::Default
            , EGPUBufferBindFlag_ShaderResource
            , BVHNodes.data()
            , D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE ) );

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

        m_MaterialIdsBuffer.Reset( GPUBuffer::CreateStructured(
              sizeof( uint32_t ) * (uint32_t)materialIds.size()
            , sizeof( uint32_t )
            , EGPUBufferUsage::Default
            , EGPUBufferBindFlag_ShaderResource
            , materialIds.data()
            , D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE ) );

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
            const DirectX::XMFLOAT4X3& transform = m_InstanceTransforms[ m_OriginalInstanceIndices[ i ] ];
            *dest = DirectX::XMFLOAT4X3( transform._11, transform._21, transform._31, transform._41, transform._12, transform._22, transform._32, transform._42, transform._13, transform._23, transform._33, transform._43 );
            ++dest;
        }

        DirectX::XMFLOAT4X3 rowMajorMatrix;
        for ( uint32_t i = 0; i < (uint32_t)m_InstanceTransforms.size(); ++i )
        {
            const DirectX::XMFLOAT4X3& transform = m_InstanceTransforms[ m_OriginalInstanceIndices[ i ] ];
            DirectX::XMMATRIX vMatrix = DirectX::XMLoadFloat4x3( &transform );
            DirectX::XMVECTOR vDet;
            vMatrix = DirectX::XMMatrixInverse( &vDet, vMatrix );
            DirectX::XMStoreFloat4x3( &rowMajorMatrix, vMatrix );
            *dest = DirectX::XMFLOAT4X3( rowMajorMatrix._11, rowMajorMatrix._21, rowMajorMatrix._31, rowMajorMatrix._41, rowMajorMatrix._12, rowMajorMatrix._22, rowMajorMatrix._32, rowMajorMatrix._42, rowMajorMatrix._13, rowMajorMatrix._23, rowMajorMatrix._33, rowMajorMatrix._43 );
            ++dest;
        }

        m_InstanceTransformsBuffer.Reset( GPUBuffer::CreateStructured(
              sizeof( DirectX::XMFLOAT4X3 ) * (uint32_t)instanceTransforms.size()
            , sizeof( DirectX::XMFLOAT4X3 )
            , EGPUBufferUsage::Default
            , EGPUBufferBindFlag_ShaderResource
            , instanceTransforms.data()
            , D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE ) );

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
            const uint32_t originalInstanceIndex = light.m_InstanceIndex;
            const uint32_t reorderedInstanceIndex = m_ReorderedInstanceIndices[ originalInstanceIndex ];
            instanceLightIndices[ reorderedInstanceIndex ] = lightIndex;
            ++lightIndex;
        }

        // Create instance light indices buffer
        m_InstanceLightIndicesBuffer.Reset( GPUBuffer::Create(
              sizeof( uint32_t ) * (uint32_t)m_InstanceTransforms.size()
            , sizeof( uint32_t )
            , DXGI_FORMAT_R32_UINT
            , EGPUBufferUsage::Default
            , EGPUBufferBindFlag_ShaderResource
            , instanceLightIndices.data()
            , D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE ) );

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
    
    m_MaterialsBuffer.Reset( GPUBuffer::CreateStructured(
          uint32_t( sizeof( GPU::Material ) * m_Materials.size() )
        , sizeof( GPU::Material )
        , EGPUBufferUsage::Default
        , EGPUBufferBindFlag_ShaderResource ) );

    if ( m_MaterialsBuffer )
    {
        LOG_STRING_FORMAT( "Material buffer created, size %d\n", sizeof( GPU::Material )* m_Materials.size() );
    }
    else 
    {
        LOG_STRING( "Failed to create materials buffer.\n" );
        return false;
    }

    m_LightsBuffer.Reset( GPUBuffer::CreateStructured(
          sizeof( GPU::SLight ) * s_MaxLightsCount
        , sizeof( GPU::SLight )
        , EGPUBufferUsage::Default
        , EGPUBufferBindFlag_ShaderResource ) );

    if ( m_LightsBuffer )
    {
        LOG_STRING_FORMAT( "Light buffer created, size %d\n", sizeof( GPU::SLight )* s_MaxLightsCount );
    }
    else 
    {
        LOG_STRING( "Failed to create lights buffer.\n" );
        return false;
    }

    // Create new textures
    {
        m_GPUTextures.reserve( m_Textures.size() );

        std::vector<D3D12_SUBRESOURCE_DATA> initialData;
        initialData.resize( 1 );
        for ( size_t textureIndex = textureIndexBase; textureIndex < m_Textures.size(); ++textureIndex )
        {
            CTexture& texture = m_Textures[ textureIndex ];
            CD3D12ResourcePtr<GPUTexture> newGPUTexture;
            if ( texture.IsValid() )
            {
                D3D12_SUBRESOURCE_DATA& subresource = initialData[ 0 ];
                subresource.pData = texture.m_PixelData.data();
                subresource.RowPitch = texture.CalculateRowPitch();
                subresource.SlicePitch = 0;

                newGPUTexture = GPUTexture::Create( texture.m_Width, texture.m_Height, GetDXGIFormat( texture.m_PixelFormat ),
                    EGPUTextureBindFlag_ShaderResource, initialData, 1U, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, L"SceneTexture" );
            }
            m_GPUTextures.emplace_back( newGPUTexture );
        }
    }

    if ( !RecreateFilmTextures() )
    {
        return false;
    }

    m_Camera.SetDirty();

    m_IsLightBufferRead = true;
    m_IsMaterialBufferRead = true;

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
    m_CameraType = ECameraType::ThinLens;
    m_FoVX = 1.221730f;
    m_FocalLength = 0.05f;
    m_FocalDistance = 2.0f;
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
    m_TLAS.clear();
    m_OriginalInstanceIndices.clear();
    m_ReorderedInstanceIndices.clear();
    m_InstanceTransforms.clear();
    m_Textures.clear();

    m_GPUTextures.clear();
    m_TextureDescriptorTable.ptr = 0;

    m_HasValidScene = false;
    m_ObjectSelection.DeselectAll();
}

void CScene::UpdateLightGPUData()
{
    GPUBuffer::SUploadContext context = {};
    if ( m_LightsBuffer->AllocateUploadContext( &context ) )
    {
        void* address = context.Map();
        if ( address )
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
                const uint32_t originalInstanceIndex = CPULight->m_InstanceIndex;
                GPULight->position_or_triangleRange.x = *(float*)&meshTriangleOffsets[ originalInstanceIndex ];
                uint32_t triangleCount = m_Meshes[ originalInstanceIndex ].GetTriangleCount();
                GPULight->position_or_triangleRange.y = *(float*)&triangleCount;
                const uint32_t reorderedInstanceIndex = m_ReorderedInstanceIndices[ originalInstanceIndex ];
                GPULight->position_or_triangleRange.z = *(float*)&reorderedInstanceIndex;
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

            context.Unmap();
            context.Upload();

            m_IsLightBufferRead = false;
        }
    }
}

static uint32_t TranslateToMaterialType( EMaterialType materialType )
{
    return (uint32_t)materialType;
}

void CScene::UpdateMaterialGPUData()
{
    GPUBuffer::SUploadContext context = {};
    if ( m_MaterialsBuffer->AllocateUploadContext( &context ) )
    {
        void* address = context.Map();
        if ( address )
        { 
            for ( uint32_t i = 0; i < (uint32_t)m_Materials.size(); ++i )
            {
                SMaterial* materialSetting = m_Materials.data() + i;
                GPU::Material* material = ( (GPU::Material*)address ) + i;
                material->albedo = materialSetting->m_MaterialType == EMaterialType::Conductor ? materialSetting->m_K : materialSetting->m_Albedo;
                material->albedoTextureIndex = materialSetting->m_MaterialType == EMaterialType::Conductor || materialSetting->m_MaterialType == EMaterialType::Dielectric ?
                    INDEX_NONE : materialSetting->m_AlbedoTextureIndex;
                material->ior = materialSetting->m_IOR;
                material->roughness = std::clamp( materialSetting->m_Roughness, 0.0f, 1.0f );
                material->texTiling = materialSetting->m_Tiling;
                material->flags = TranslateToMaterialType( materialSetting->m_MaterialType ) & MATERIAL_FLAG_TYPE_MASK;
                material->flags |= materialSetting->m_Multiscattering ? MATERIAL_FLAG_MULTISCATTERING : 0;
                material->flags |= materialSetting->m_IsTwoSided ? MATERIAL_FLAG_IS_TWOSIDED : 0;
                material->flags |= materialSetting->m_HasRoughnessTexture ? MATERIAL_FLAG_ROUGHNESS_TEXTURE : 0;
            }
            context.Unmap();
            context.Upload();

            m_IsMaterialBufferRead = false;
        }
    }
}

void CScene::AllocateAndUpdateTextureDescriptorTable()
{
    CD3D12GPUDescriptorHeap* descriptorHeap = D3D12Adapter::GetGPUDescriptorHeap();
    if ( m_GPUTextures.size() )
    {
        SD3D12GPUDescriptorHeapHandle descriptortable = descriptorHeap->AllocateRange( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, (uint32_t)m_GPUTextures.size() );

        const SD3D12DescriptorHandle nullSRV = D3D12Adapter::GetNullBufferSRV();
        CD3DX12_CPU_DESCRIPTOR_HANDLE dstSRV( descriptortable.m_CPU );
        for ( size_t index = 0; index < m_GPUTextures.size(); ++index )
        {
            GPUTexture* texture = m_GPUTextures[ index ].Get();
            D3D12_CPU_DESCRIPTOR_HANDLE srcSRV = texture ? texture->GetSRV().CPU : nullSRV.CPU;
            D3D12Adapter::GetDevice()->CopyDescriptorsSimple( 1, dstSRV, srcSRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
            dstSRV.Offset( 1, D3D12Adapter::GetDescriptorSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ) );
        }

        m_TextureDescriptorTable = descriptortable.m_GPU;
    }
    else
    {
        // Although the shader won't access this descriptor table, the D3D runtime requires a valid table to be set
        m_TextureDescriptorTable = descriptorHeap->GetD3DHeap()->GetGPUDescriptorHandleForHeapStart();
    }
}

// Calculate film distance from focal length and focal distance.
// Based on the Gaussian lens equation.
float CScene::CalculateFilmDistance() const
{
    return m_CameraType == ECameraType::PinHole ?
        0.5f * m_FilmSize.x / std::max( tanf( 0.5f * m_FoVX ), 0.0001f ) :
        ( m_FocalLength * m_FocalDistance ) / ( m_FocalLength + m_FocalDistance );
}

float CScene::CalculateApertureDiameter() const
{
    return m_CameraType == ECameraType::PinHole ? 0.f : m_FocalLength / m_RelativeAperture;
}

bool CScene::RecreateFilmTextures()
{
    m_FilmTexture.Reset( GPUTexture::Create(
          m_ResolutionWidth
        , m_ResolutionHeight
        , DXGI_FORMAT_R32G32B32A32_FLOAT
        , EGPUTextureBindFlag_UnorderedAccess | EGPUTextureBindFlag_RenderTarget
        , 1
        , D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE ) );
    if ( !m_FilmTexture )
    {
        return false;
    }

    m_SamplePositionTexture.Reset( GPUTexture::Create(
          m_ResolutionWidth
        , m_ResolutionHeight
        , DXGI_FORMAT_R32G32_FLOAT
        , EGPUTextureBindFlag_UnorderedAccess
        , 1
        , D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE ) );
    if ( !m_SamplePositionTexture )
    { 
        return false;
    }

    m_SampleValueTexture.Reset( GPUTexture::Create(
          m_ResolutionWidth
        , m_ResolutionHeight
        , DXGI_FORMAT_R32G32B32A32_FLOAT
        , EGPUTextureBindFlag_UnorderedAccess
        , 1
        , D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE ) );
    if ( !m_SampleValueTexture )
    {
        return false;
    }

    m_RenderResultTexture.Reset( GPUTexture::Create(
          m_ResolutionWidth
        , m_ResolutionHeight
        , DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
        , EGPUTextureBindFlag_RenderTarget
        , 1
        , D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE ) );
    if ( !m_RenderResultTexture )
    { 
        return false;
    }

    m_FilmTextureStates = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
    m_IsSampleTexturesRead = true;
    m_IsRenderResultTextureRead = true;

    return true;
}

bool SEnvironmentLight::CreateTextureFromFile()
{
    std::wstring filename = StringConversion::UTF8StringToUTF16WString( m_TextureFileName );
    m_Texture.Reset( GPUTexture::CreateFromFile( filename.c_str() ) );
    return m_Texture.Get() != nullptr;
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

