#include "stdafx.h"
#include "Scene.h"
#include "CommandLineArgs.h"
#include "Mesh.h"
#include "Logging.h"
#include "MessageBox.h"
#include "GPUBuffer.h"
#include "GPUTexture.h"
#include "../Shaders/Light.inc.hlsl"
#include "imgui/imgui.h"

using namespace DirectX;

bool CScene::LoadFromFile( const char* filepath )
{
    if ( filepath == nullptr || filepath[ 0 ] == '\0' )
        return false;

    if ( !LoadFromXMLFile( filepath ) )
        return false;

    m_VerticesBuffer.reset( GPUBuffer::CreateStructured(
          sizeof( Vertex ) * m_Mesh.GetVertexCount()
        , sizeof( Vertex )
        , D3D11_USAGE_IMMUTABLE
        , D3D11_BIND_SHADER_RESOURCE
        , 0
        , m_Mesh.GetVertices().data() ) );
    if ( !m_VerticesBuffer )
    {
        CMessagebox::GetSingleton().Append( "Failed to create vertices buffer.\n" );
        return false;
    }

    m_TrianglesBuffer.reset( GPUBuffer::CreateStructured(
          sizeof( uint32_t ) * m_Mesh.GetIndexCount()
        , sizeof( uint32_t )
        , D3D11_USAGE_IMMUTABLE
        , D3D11_BIND_SHADER_RESOURCE
        , 0
        , m_Mesh.GetIndices().data() ) );
    if ( !m_TrianglesBuffer )
    {
        CMessagebox::GetSingleton().Append( "Failed to create triangles buffer.\n" );
        return false;
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
          uint32_t( sizeof( Material ) * m_Mesh.GetMaterials().size() )
        , sizeof( Material )
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
              sizeof( BVHNode ) * m_Mesh.GetBVHNodeCount()
            , sizeof( BVHNode )
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
        sizeof( SLight ) * s_MaxLightsCount
        , sizeof( SLight )
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
    m_LightSettings.clear();

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
        for ( uint32_t i = 0; i < (uint32_t)m_LightSettings.size(); ++i )
        {
            SLightSetting* lightSetting = m_LightSettings.data() + i;
            SLight* light = ( (SLight*)address ) + i;

            XMVECTOR xmPosition = XMLoadFloat3( &lightSetting->position );
            XMVECTOR xmQuat = XMQuaternionRotationRollPitchYaw( lightSetting->rotation.x, lightSetting->rotation.y, lightSetting->rotation.z );
            XMFLOAT3 size3 = XMFLOAT3( lightSetting->size.x, lightSetting->size.y, 1.0f );
            XMVECTOR xmScale = XMLoadFloat3( &size3 );
            XMMATRIX xmTransform = XMMatrixAffineTransformation( xmScale, g_XMZero, xmQuat, xmPosition );

            // Shader uses column major
            xmTransform = XMMatrixTranspose( xmTransform );
            XMFLOAT4X4 transform44;
            XMStoreFloat4x4( &transform44, xmTransform );
            light->transform = XMFLOAT4X3( (float*)&transform44 );

            light->color = lightSetting->color;

            switch ( lightSetting->lightType )
            {
            case ELightType::Point:
            {
                light->flags = LIGHT_FLAGS_POINT_LIGHT;
                break;
            }
            case ELightType::Rectangle:
            {
                light->flags = 0;
                break;
            }
            default:
            {
                light->flags = 0;
                break;
            }
            }
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
            SMaterialSetting* materialSetting = materials.data() + i;
            Material* material = ( (Material*)address ) + i;
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

