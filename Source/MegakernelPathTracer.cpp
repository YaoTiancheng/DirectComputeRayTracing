#include "stdafx.h"
#include "MegakernelPathTracer.h"
#include "Shader.h"
#include "GPUBuffer.h"
#include "GPUTexture.h"
#include "Scene.h"
#include "ComputeJob.h"
#include "RenderContext.h"
#include "RenderData.h"
#include "MessageBox.h"
#include "ScopedRenderAnnotation.h"
#include "imgui/imgui.h"
#include "../Shaders/LightSharedDef.inc.hlsl"

using namespace DirectX;

#define CS_GROUP_SIZE_X 16
#define CS_GROUP_SIZE_Y 8

struct SRayTracingConstants
{
    DirectX::XMFLOAT4X4 cameraTransform;
    DirectX::XMFLOAT2   filmSize;
    uint32_t            resolutionX;
    uint32_t            resolutionY;
    uint32_t            maxBounceCount;
    uint32_t            lightCount;
    float               apertureRadius;
    float               focalDistance;
    float               filmDistance;
    DirectX::XMFLOAT2   bladeVertexPos;
    uint32_t            bladeCount;
    float               apertureBaseAngle;
    uint32_t            tileOffsetX;
    uint32_t            tileOffsetY;
    uint32_t            environmentLightIndex;
};

struct SDebugConstants
{
    uint32_t m_IterationThreshold;
    uint32_t m_Padding[ 3 ];
};

bool CMegakernelPathTracer::Create()
{
    m_RayTracingConstantsBuffer.reset( GPUBuffer::Create(
          sizeof( SRayTracingConstants )
        , 0
        , DXGI_FORMAT_UNKNOWN
        , D3D11_USAGE_DYNAMIC
        , D3D11_BIND_CONSTANT_BUFFER
        , GPUResourceCreationFlags_CPUWriteable ) );
    if ( !m_RayTracingConstantsBuffer )
        return false;

    m_DebugConstantsBuffer.reset( GPUBuffer::Create(
          sizeof( SDebugConstants )
        , 0
        , DXGI_FORMAT_UNKNOWN
        , D3D11_USAGE_DYNAMIC
        , D3D11_BIND_CONSTANT_BUFFER
        , GPUResourceCreationFlags_CPUWriteable ) );
    if ( !m_DebugConstantsBuffer )
        return false;

    return true;
}

void CMegakernelPathTracer::Destroy()
{
    m_DebugConstantsBuffer.reset();
    m_RayTracingConstantsBuffer.reset();
    m_RayTracingShader.reset();
}

void CMegakernelPathTracer::OnSceneLoaded()
{
    CompileAndCreateRayTracingKernel();
    m_FilmClearTrigger = true;
}

void CMegakernelPathTracer::Render( const SRenderContext& renderContext, const SRenderData& renderData )
{
    SCOPED_RENDER_ANNOTATION( L"Dispatch rays" );

    uint32_t tileCountX = (uint32_t)std::ceilf( float( renderContext.m_CurrentResolutionWidth ) / float( m_TileSize ) );
    uint32_t tileCountY = (uint32_t)std::ceilf( float( renderContext.m_CurrentResolutionHeight ) / float( m_TileSize ) );

    if ( void* address = m_RayTracingConstantsBuffer->Map() )
    {
        SRayTracingConstants* constants = (SRayTracingConstants*)address;
        constants->resolutionX = renderContext.m_CurrentResolutionWidth;
        constants->resolutionY = renderContext.m_CurrentResolutionHeight;
        m_Scene->m_Camera.GetTransformMatrix( &constants->cameraTransform );
        constants->filmDistance = m_Scene->GetFilmDistance();
        constants->filmSize = m_Scene->m_FilmSize;
        constants->lightCount = m_Scene->GetLightCount();
        constants->maxBounceCount = m_Scene->m_MaxBounceCount;
        constants->apertureRadius = m_Scene->CalculateApertureDiameter() * 0.5f;
        constants->focalDistance = m_Scene->m_FocalDistance;
        constants->apertureBaseAngle = m_Scene->m_ApertureRotation;
        constants->bladeCount = m_Scene->m_ApertureBladeCount;

        float halfBladeAngle = DirectX::XM_PI / m_Scene->m_ApertureBladeCount;
        constants->bladeVertexPos.x = cosf( halfBladeAngle ) * constants->apertureRadius;
        constants->bladeVertexPos.y = sinf( halfBladeAngle ) * constants->apertureRadius;

        constants->tileOffsetX = ( m_CurrentTileIndex % tileCountX ) * m_TileSize;
        constants->tileOffsetY = ( m_CurrentTileIndex / tileCountX ) * m_TileSize;

        constants->environmentLightIndex = m_Scene->m_EnvironmentLight ? (uint32_t)m_Scene->m_MeshLights.size() : LIGHT_INDEX_INVALID; // Environment light is right after the mesh lights.

        m_RayTracingConstantsBuffer->Unmap();
    }
    if ( m_OutputType > 0 )
    {
        if ( void* address = m_DebugConstantsBuffer->Map() )
        {
            SDebugConstants* constants = (SDebugConstants*)address;
            constants->m_IterationThreshold = m_IterationThreshold;
            m_DebugConstantsBuffer->Unmap();
        }
    }

    ComputeJob computeJob;
    computeJob.m_SamplerStates = { renderData.m_UVClampSamplerState.Get() };
    computeJob.m_UAVs = { renderData.m_SamplePositionTexture->GetUAV(), renderData.m_SampleValueTexture->GetUAV() };

    ID3D11ShaderResourceView* environmentTextureSRV = nullptr;
    if ( m_Scene->m_EnvironmentLight && m_Scene->m_EnvironmentLight->m_Texture )
    {
        environmentTextureSRV = m_Scene->m_EnvironmentLight->m_Texture->GetSRV();
    }

    computeJob.m_SRVs = {
          m_Scene->m_VerticesBuffer->GetSRV()
        , m_Scene->m_TrianglesBuffer->GetSRV()
        , m_Scene->m_LightsBuffer->GetSRV()
        , renderData.m_CookTorranceCompETexture->GetSRV()
        , renderData.m_CookTorranceCompEAvgTexture->GetSRV()
        , renderData.m_CookTorranceCompInvCDFTexture->GetSRV()
        , renderData.m_CookTorranceCompPdfScaleTexture->GetSRV()
        , renderData.m_CookTorranceCompEFresnelTexture->GetSRV()
        , renderData.m_CookTorranceBSDFETexture->GetSRV()
        , renderData.m_CookTorranceBSDFAvgETexture->GetSRV()
        , renderData.m_CookTorranceBTDFETexture->GetSRV()
        , renderData.m_CookTorranceBSDFInvCDFTexture->GetSRV()
        , renderData.m_CookTorranceBSDFPDFScaleTexture->GetSRV()
        , m_Scene->m_BVHNodesBuffer ? m_Scene->m_BVHNodesBuffer->GetSRV() : nullptr
        , m_Scene->m_InstanceTransformsBuffer->GetSRV( 0, (uint32_t)m_Scene->m_InstanceTransforms.size() )
        , m_Scene->m_InstanceTransformsBuffer->GetSRV( (uint32_t)m_Scene->m_InstanceTransforms.size(), (uint32_t)m_Scene->m_InstanceTransforms.size() )
        , m_Scene->m_MaterialIdsBuffer->GetSRV()
        , m_Scene->m_MaterialsBuffer->GetSRV()
        , m_Scene->m_InstanceLightIndicesBuffer->GetSRV()
        , environmentTextureSRV
    };

    computeJob.m_ConstantBuffers = { m_RayTracingConstantsBuffer->GetBuffer(), renderData.m_RayTracingFrameConstantBuffer->GetBuffer(), m_DebugConstantsBuffer->GetBuffer() };
    computeJob.m_Shader = m_RayTracingShader.get();

    uint32_t dispatchThreadWidth = renderContext.m_IsSmallResolutionEnabled ? renderContext.m_CurrentResolutionWidth : m_TileSize;
    uint32_t dispatchThreadHeight = renderContext.m_IsSmallResolutionEnabled ? renderContext.m_CurrentResolutionHeight : m_TileSize;
    computeJob.m_DispatchSizeX = (uint32_t)ceil( dispatchThreadWidth / (float)CS_GROUP_SIZE_X );
    computeJob.m_DispatchSizeY = (uint32_t)ceil( dispatchThreadHeight / (float)CS_GROUP_SIZE_Y );
    computeJob.m_DispatchSizeZ = 1;

    computeJob.Dispatch();

    uint32_t tileCount = tileCountX * tileCountY;
    m_CurrentTileIndex = ( m_CurrentTileIndex + 1 ) % tileCount;
}

void CMegakernelPathTracer::ResetImage()
{
    ResetTileIndex();
}

bool CMegakernelPathTracer::IsImageComplete()
{
    return AreAllTilesRendered();
}

bool CMegakernelPathTracer::CompileAndCreateRayTracingKernel()
{
    std::vector<D3D_SHADER_MACRO> rayTracingShaderDefines;

    static const uint32_t s_MaxRadix10IntegerBufferLengh = 12;
    char buffer_TraversalStackSize[ s_MaxRadix10IntegerBufferLengh ];
    _itoa( m_Scene->m_BVHTraversalStackSize, buffer_TraversalStackSize, 10 );
    rayTracingShaderDefines.push_back( { "RT_BVH_TRAVERSAL_STACK_SIZE", buffer_TraversalStackSize } );

    char buffer_TraversalGroupSize[ s_MaxRadix10IntegerBufferLengh ];
    _itoa( CS_GROUP_SIZE_X * CS_GROUP_SIZE_Y, buffer_TraversalGroupSize, 10 );
    rayTracingShaderDefines.push_back( { "RT_BVH_TRAVERSAL_GROUP_SIZE", buffer_TraversalGroupSize } );

    rayTracingShaderDefines.push_back( { "GROUP_SIZE_X", DCRT_STRINGIFY_MACRO_VALUE( CS_GROUP_SIZE_X ) } );
    rayTracingShaderDefines.push_back( { "GROUP_SIZE_Y", DCRT_STRINGIFY_MACRO_VALUE( CS_GROUP_SIZE_Y ) } );

    if ( m_Scene->m_IsGGXVNDFSamplingEnabled )
    {
        rayTracingShaderDefines.push_back( { "GGX_SAMPLE_VNDF", "0" } );
    }
    if ( !m_Scene->m_TraverseBVHFrontToBack )
    {
        rayTracingShaderDefines.push_back( { "BVH_NO_FRONT_TO_BACK_TRAVERSAL", "0" } );
    }
    if ( m_Scene->m_IsLightVisible )
    {
        rayTracingShaderDefines.push_back( { "LIGHT_VISIBLE", "0" } );
    }
    if ( m_Scene->m_EnvironmentLight && m_Scene->m_EnvironmentLight->m_Texture )
    {
        rayTracingShaderDefines.push_back( { "HAS_ENV_TEXTURE", "0" } );
    }
    static const char* s_RayTracingOutputDefines[] = { "MEGAKERNEL", "OUTPUT_NORMAL", "OUTPUT_TANGENT", "OUTPUT_ALBEDO", "OUTPUT_NEGATIVE_NDOTV", "OUTPUT_BACKFACE", "OUTPUT_ITERATION_COUNT" };
    if ( s_RayTracingOutputDefines[ m_OutputType ] )
    {
        rayTracingShaderDefines.push_back( { s_RayTracingOutputDefines[ m_OutputType ], "0" } );
    }
    rayTracingShaderDefines.push_back( { NULL, NULL } );

    m_RayTracingShader.reset( ComputeShader::CreateFromFile( L"Shaders\\MegakernelPathTracing.hlsl", rayTracingShaderDefines ) );
    if ( !m_RayTracingShader )
    {
        CMessagebox::GetSingleton().Append( "Failed to compile ray tracing shader.\n" );
        return false;
    }

    return true;
}

void CMegakernelPathTracer::ResetTileIndex()
{
    m_CurrentTileIndex = 0;
}

bool CMegakernelPathTracer::AreAllTilesRendered() const
{
    return m_CurrentTileIndex == 0;
}

void CMegakernelPathTracer::OnImGUI()
{
    if ( ImGui::CollapsingHeader( "Megakernel Path Tracer" ) )
    {
        if ( ImGui::InputInt( "Render Tile Size", (int*)&m_TileSize, 16, 32, ImGuiInputTextFlags_EnterReturnsTrue ) )
        {
            m_TileSize = std::max( (uint32_t)16, m_TileSize );
            m_FilmClearTrigger = true;
        }

        static const char* s_OutputNames[] = { "Path Tracing", "Shading Normal", "Shading Tangent", "Albedo", "Negative NdotV", "Backface", "Iteration Count"};
        if ( ImGui::Combo( "Output", (int*)&m_OutputType, s_OutputNames, IM_ARRAYSIZE( s_OutputNames ) ) )
        {
            CompileAndCreateRayTracingKernel();
            m_FilmClearTrigger = true;
        }
        
        if ( m_OutputType == 6 )
        {
            if ( ImGui::DragInt( "Iteration Threshold", (int*)&m_IterationThreshold, 1.f, 1, 10000, "%d", ImGuiSliderFlags_AlwaysClamp ) )
            {
                m_FilmClearTrigger = true;
            }
        }
    }
}

bool CMegakernelPathTracer::AcquireFilmClearTrigger()
{
    bool value = m_FilmClearTrigger;
    m_FilmClearTrigger = false;
    return value;
}
