#include "stdafx.h"
#include "WavefrontPathTracer.h"
#include "Scene.h"
#include "Shader.h"
#include "GPUBuffer.h"
#include "GPUTexture.h"
#include "MessageBox.h"
#include "D3D11RenderSystem.h"
#include "ComputeJob.h"
#include "RenderContext.h"
#include "RenderData.h"
#include "ScopedRenderAnnotation.h"
#include "imgui/imgui.h"

using namespace DirectX;

static const uint32_t s_WavefrontSize = 32;
static const uint32_t s_PathPoolSize = 8192;
static const uint32_t s_PathPoolLaneCount = s_PathPoolSize * s_WavefrontSize;
static const uint32_t s_KernelGroupSize = 32;

static const uint32_t s_BlockDimensionCount = 2;

struct SControlConstants
{
    XMFLOAT4 g_Background;
    uint32_t g_PathCount;
    uint32_t g_MaxBounceCount;
    uint32_t g_BlockCounts[ 2 ];
    uint32_t g_BlockDimension[ 2 ];
    uint32_t g_FilmDimension[ 2 ];
};

struct SNewPathConstants
{
    XMFLOAT4X4 g_CameraTransform;
    uint32_t g_Resolution[ 2 ];
    XMFLOAT2 g_FilmSize;
    float g_ApertureRadius;
    float g_FocalDistance;
    float g_FilmDistance;
    uint32_t g_BladeCount;
    XMFLOAT2 g_BladeVertexPos;
    float g_ApertureBaseAngle;
    uint32_t padding;
};

struct SMaterialConstants
{
    uint32_t g_LightCount;
    uint32_t padding[ 3 ];
};

bool CWavefrontPathTracer::Create()
{
    m_RayBuffer.reset( GPUBuffer::CreateStructured(
          s_PathPoolLaneCount * 32
        , 32
        , D3D11_USAGE_DEFAULT
        , D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS ) );
    if ( !m_RayBuffer )
        return false;

    m_RayHitBuffer.reset( GPUBuffer::CreateStructured(
          s_PathPoolLaneCount * 20
        , 20
        , D3D11_USAGE_DEFAULT
        , D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS ) );
    if ( !m_RayHitBuffer )
        return false;

    m_ShadowRayBuffer.reset( GPUBuffer::CreateStructured(
          s_PathPoolLaneCount * 32
        , 32
        , D3D11_USAGE_DEFAULT
        , D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS ) );
    if ( !m_ShadowRayBuffer )
        return false;

    m_PixelPositionBuffer.reset( GPUBuffer::CreateStructured(
          s_PathPoolLaneCount * 8
        , 8
        , D3D11_USAGE_DEFAULT
        , D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS ) );
    if ( !m_PixelPositionBuffer )
        return false;

    m_PixelSampleBuffer.reset( GPUBuffer::CreateStructured(
          s_PathPoolLaneCount * 8
        , 8
        , D3D11_USAGE_DEFAULT
        , D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS ) );
    if ( !m_PixelSampleBuffer )
        return false;

    m_RngBuffer.reset( GPUBuffer::CreateStructured(
          s_PathPoolLaneCount * 16
        , 16
        , D3D11_USAGE_DEFAULT
        , D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS ) );
    if ( !m_RngBuffer )
        return false;

    m_MISResultBuffer.reset( GPUBuffer::CreateStructured(
          s_PathPoolLaneCount * 28
        , 28
        , D3D11_USAGE_DEFAULT
        , D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS ) );
    if ( !m_MISResultBuffer )
        return false;

    m_PathAccumulationBuffer.reset( GPUBuffer::CreateStructured(
          s_PathPoolLaneCount * 24
        , 24
        , D3D11_USAGE_DEFAULT
        , D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS ) );
    if ( !m_PathAccumulationBuffer )
        return false;

    m_FlagsBuffer.reset( GPUBuffer::Create(
          s_PathPoolLaneCount * 4
        , 4
        , DXGI_FORMAT_R32_UINT
        , D3D11_USAGE_DEFAULT
        , D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS ) );
    if ( !m_FlagsBuffer )
        return false;

    m_NextBlockIndexBuffer.reset( GPUBuffer::Create(
          4
        , 4
        , DXGI_FORMAT_R32_UINT
        , D3D11_USAGE_DEFAULT
        , D3D11_BIND_UNORDERED_ACCESS ) );
    if ( !m_NextBlockIndexBuffer )
        return false;

    for ( uint32_t i = 0; i < 4; ++i )
    {
        m_IndirectArgumentBuffer[ i ].reset( GPUBuffer::Create(
              12
            , 4
            , DXGI_FORMAT_R32_UINT
            , D3D11_USAGE_DEFAULT
            , D3D11_BIND_UNORDERED_ACCESS
            , GPUResourceCreationFlags::GPUResourceCreationFlags_IndirectArgs ) );
        if ( !m_IndirectArgumentBuffer[ i ] )
            return false;

        m_QueueBuffers[ i ].reset( GPUBuffer::Create(
              sizeof( uint32_t ) * s_PathPoolLaneCount
            , sizeof( uint32_t )
            , DXGI_FORMAT_R32_UINT
            , D3D11_USAGE_DEFAULT
            , D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE ) );
        if ( !m_QueueBuffers[ i ] )
            return false;
    }

    for ( uint32_t i = 0; i < 2; ++i )
    {
        m_QueueCounterBuffers[ i ].reset( GPUBuffer::Create(
              16
            , 4
            , DXGI_FORMAT_R32_UINT
            , D3D11_USAGE_DEFAULT
            , D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS ) );
        if ( !m_QueueCounterBuffers[ i ] )
            return false;

        m_QueueConstantsBuffers[ i ].reset( GPUBuffer::Create(
              16
            , 4
            , DXGI_FORMAT_R32_UINT
            , D3D11_USAGE_DEFAULT
            , D3D11_BIND_CONSTANT_BUFFER ) );
        if ( !m_QueueConstantsBuffers[ i ] )
            return false;
    }

    for ( uint32_t i = 0; i < s_QueueCounterStagingBufferCount; ++i )
    {
        m_QueueCounterStagingBuffer[ i ].reset( GPUBuffer::Create(
              16
            , 4
            , DXGI_FORMAT_R32_UINT
            , D3D11_USAGE_STAGING
            , 0
            , GPUResourceCreationFlags_CPUReadable ) );
        if ( !m_QueueCounterStagingBuffer[ i ] )
            return false;
    }

    m_ControlConstantBuffer.reset( GPUBuffer::Create(
          sizeof( SControlConstants )
        , 0
        , DXGI_FORMAT_UNKNOWN
        , D3D11_USAGE_DYNAMIC
        , D3D11_BIND_CONSTANT_BUFFER
        , GPUResourceCreationFlags_CPUWriteable ) );
    if ( !m_ControlConstantBuffer )
        return false;

    m_NewPathConstantBuffer.reset( GPUBuffer::Create(
          sizeof( SNewPathConstants )
        , 0
        , DXGI_FORMAT_UNKNOWN
        , D3D11_USAGE_DYNAMIC
        , D3D11_BIND_CONSTANT_BUFFER
        , GPUResourceCreationFlags_CPUWriteable ) );
    if ( !m_NewPathConstantBuffer )
        return false;

    m_MaterialConstantBuffer.reset( GPUBuffer::Create(
          sizeof( SMaterialConstants )
        , 0
        , DXGI_FORMAT_UNKNOWN
        , D3D11_USAGE_DYNAMIC
        , D3D11_BIND_CONSTANT_BUFFER
        , GPUResourceCreationFlags_CPUWriteable ) );
    if ( !m_MaterialConstantBuffer )
        return false;

    return true;
}

void CWavefrontPathTracer::Destroy()
{
    m_RayBuffer.reset();
    m_RayHitBuffer.reset();
    m_ShadowRayBuffer.reset();
    m_PixelPositionBuffer.reset();
    m_PixelSampleBuffer.reset();
    m_RngBuffer.reset();
    m_MISResultBuffer.reset();
    m_PathAccumulationBuffer.reset();
    m_FlagsBuffer.reset();
    m_NextBlockIndexBuffer.reset();

    for ( uint32_t i = 0; i < 4; ++i )
    {
        m_IndirectArgumentBuffer[ i ].reset();
        m_QueueBuffers[ i ].reset();
    }

    for ( uint32_t i = 0; i < 2; ++i )
    {
        m_QueueCounterBuffers[ i ].reset();
        m_QueueConstantsBuffers[ i ].reset();
    }

    for ( uint32_t i = 0; i < s_QueueCounterStagingBufferCount; ++i )
    {
        m_QueueCounterStagingBuffer[ i ].reset();
    }

    m_ControlConstantBuffer.reset();
    m_NewPathConstantBuffer.reset();
    m_MaterialConstantBuffer.reset();

    for ( uint32_t i = 0; i < (uint32_t)EShaderKernel::_Count; ++i )
    {
        m_Shaders[ i ].reset();
    }
}

void CWavefrontPathTracer::OnSceneLoaded()
{
    for ( int i = 0; i < (int)EShaderKernel::_Count; ++i )
    {
        CompileAndCreateShader( (EShaderKernel)i );
    }
}

uint32_t CalculateDispatchGroupCount( uint32_t laneCount )
{
    uint32_t groupCount = laneCount / s_KernelGroupSize;
    if ( laneCount % s_KernelGroupSize != 0 )
    {
        groupCount += 1;
    }
    return groupCount;
}

void CWavefrontPathTracer::Render( const SRenderContext& renderContext, const SRenderData& renderData )
{
    ID3D11Device* device = GetDevice();
    ID3D11DeviceContext* deviceContext = GetDeviceContext();

    uint32_t blockWidth, blockHeight;
    GetBlockDimension( &blockWidth, &blockHeight );

    // Fill control constants buffer
    if ( void* address = m_ControlConstantBuffer->Map() )
    {
        SControlConstants* constants = (SControlConstants*)address;
        constants->g_Background = m_Scene->m_BackgroundColor;
        constants->g_PathCount = s_PathPoolLaneCount;
        constants->g_MaxBounceCount = m_Scene->m_MaxBounceCount;
        constants->g_BlockCounts[ 0 ] = renderContext.m_CurrentResolutionWidth / blockWidth;
        if ( renderContext.m_CurrentResolutionWidth % blockWidth )
            constants->g_BlockCounts[ 0 ] += 1;
        constants->g_BlockCounts[ 1 ] = renderContext.m_CurrentResolutionHeight / blockHeight;
        if ( renderContext.m_CurrentResolutionHeight % blockHeight )
            constants->g_BlockCounts[ 1 ] += 1;
        constants->g_BlockDimension[ 0 ] = blockWidth;
        constants->g_BlockDimension[ 1 ] = blockHeight;
        constants->g_FilmDimension[ 0 ] = renderContext.m_CurrentResolutionWidth;
        constants->g_FilmDimension[ 1 ] = renderContext.m_CurrentResolutionHeight;
        m_ControlConstantBuffer->Unmap();
    }

    // Fill new path constants buffer
    if ( void* address = m_NewPathConstantBuffer->Map() )
    {
        SNewPathConstants* constants = (SNewPathConstants*)address;
        m_Scene->m_Camera.GetTransformMatrix( &constants->g_CameraTransform );
        constants->g_Resolution[ 0 ] = renderContext.m_CurrentResolutionWidth;
        constants->g_Resolution[ 1 ] = renderContext.m_CurrentResolutionHeight;
        constants->g_FilmSize = m_Scene->m_FilmSize;
        constants->g_ApertureRadius = m_Scene->CalculateApertureDiameter() * 0.5f;
        constants->g_FocalDistance = m_Scene->m_FocalDistance;
        constants->g_FilmDistance = m_Scene->GetFilmDistance();
        constants->g_BladeCount = m_Scene->m_ApertureBladeCount;
        float halfBladeAngle = DirectX::XM_PI / m_Scene->m_ApertureBladeCount;
        constants->g_BladeVertexPos.x = cosf( halfBladeAngle ) * constants->g_ApertureRadius;
        constants->g_BladeVertexPos.y = sinf( halfBladeAngle ) * constants->g_ApertureRadius;
        constants->g_ApertureBaseAngle = m_Scene->m_ApertureRotation;
        m_NewPathConstantBuffer->Unmap();
    }

    // Fill material constants buffer
    if ( void* address = m_MaterialConstantBuffer->Map() )
    {
        SMaterialConstants* constants = (SMaterialConstants*)address;
        constants->g_LightCount = m_Scene->GetLightCount();
        m_MaterialConstantBuffer->Unmap();
    }

    if ( m_NewImage )
    {
        SCOPED_RENDER_ANNOTATION( L"Reset" );

        // Clear the nextblockindex
        uint32_t value[ 4 ] = { 0 };
        deviceContext->ClearUnorderedAccessViewUint( m_NextBlockIndexBuffer->GetUAV(), value );

        // Reset all path to idle
        ComputeJob job;
        job.m_ConstantBuffers.push_back( m_ControlConstantBuffer->GetBuffer() );
        job.m_Shader = m_Shaders[ (int)EShaderKernel::SetIdle ].get();
        job.m_UAVs.push_back( m_FlagsBuffer->GetUAV() );
        job.m_DispatchSizeX = CalculateDispatchGroupCount( s_PathPoolLaneCount );
        job.m_DispatchSizeY = 1;
        job.m_DispatchSizeZ = 1;
        job.Dispatch();
    }

    for ( uint32_t i = 0; i < m_IterationPerFrame; ++i )
    {
        RenderOneIteration( renderContext, renderData );
    }    

    // Copy ray counter to the staging buffer
    {
        SCOPED_RENDER_ANNOTATION( L"Copy ray counter to staging buffer" );

        if ( m_NewImage )
        {
            // For new image, initialize all staging buffer
            for ( uint32_t i = 0; i < s_QueueCounterStagingBufferCount; ++i )
            {
                deviceContext->CopyResource( m_QueueCounterStagingBuffer[ i ]->GetBuffer(), m_QueueCounterBuffers[ 0 ]->GetBuffer() );
                m_QueueCounterStagingBufferIndex = 0;
            }
        }
        else
        {
            deviceContext->CopyResource( m_QueueCounterStagingBuffer[ m_QueueCounterStagingBufferIndex ]->GetBuffer(), m_QueueCounterBuffers[ 0 ]->GetBuffer() );
            m_QueueCounterStagingBufferIndex = ( m_QueueCounterStagingBufferIndex + 1 ) % s_QueueCounterStagingBufferCount;
        }
    }

    m_NewImage = IsImageComplete();
}

void CWavefrontPathTracer::ResetImage()
{
    m_NewImage = true;
}

bool CWavefrontPathTracer::IsImageComplete()
{
    if ( void* address = m_QueueCounterStagingBuffer[ m_QueueCounterStagingBufferIndex ]->Map( D3D11_MAP_READ, 0 ) )
    {
        uint32_t extensionRayCount = *(uint32_t*)address;
        m_QueueCounterStagingBuffer[ m_QueueCounterStagingBufferIndex ]->Unmap();
        return extensionRayCount == 0;
    }
    return false;
}

void CWavefrontPathTracer::OnImGUI()
{
    if ( ImGui::CollapsingHeader( "Wavefront Path Tracer" ) )
    {
        ImGui::DragInt( "Iteration Per-frame", (int*)&m_IterationPerFrame, 1.0f, 1, 999, "%d", ImGuiSliderFlags_AlwaysClamp );

        const char* blockDimensionNames[] = { "8x4", "4x8" };
        if ( ImGui::Combo( "Block Dimension", (int*)&m_BlockDimensionIndex, blockDimensionNames, IM_ARRAYSIZE( blockDimensionNames ) ) )
        {
            m_FilmClearTrigger = true;
        }
    }
}

bool CWavefrontPathTracer::AcquireFilmClearTrigger()
{
    bool value = m_FilmClearTrigger;
    m_FilmClearTrigger = false;
    return value;
}

bool CWavefrontPathTracer::CompileAndCreateShader( EShaderKernel kernel )
{
    std::vector<D3D_SHADER_MACRO> rayTracingShaderDefines;

    static const uint32_t s_MaxRadix10IntegerBufferLengh = 12;
    char buffer_TraversalStackSize[ s_MaxRadix10IntegerBufferLengh ];
    _itoa( m_Scene->m_BVHTraversalStackSize, buffer_TraversalStackSize, 10 );

    rayTracingShaderDefines.push_back( { "RT_BVH_TRAVERSAL_STACK_SIZE", buffer_TraversalStackSize } );

    rayTracingShaderDefines.push_back( { "RT_BVH_TRAVERSAL_GROUP_SIZE", "32" } );

    if ( m_Scene->m_IsGGXVNDFSamplingEnabled )
    {
        rayTracingShaderDefines.push_back( { "GGX_SAMPLE_VNDF", "0" } );
    }
    if ( m_Scene->m_EnvironmentTexture.get() == nullptr )
    {
        rayTracingShaderDefines.push_back( { "NO_ENV_TEXTURE", "0" } );
    }
    rayTracingShaderDefines.push_back( { NULL, NULL } );

    const char* s_KernelDefines[] = { "EXTENSION_RAY_CAST", "SHADOW_RAY_CAST", "NEW_PATH", "MATERIAL", "CONTROL", "FILL_INDIRECT_ARGUMENTS", "SET_IDLE" };
    rayTracingShaderDefines.insert( rayTracingShaderDefines.begin(), { s_KernelDefines[ (int)kernel ], "0" } );

    m_Shaders[ (int)kernel ].reset( ComputeShader::CreateFromFile( L"Shaders\\RayTracing.hlsl", rayTracingShaderDefines ) );
    if ( !m_Shaders[ (int)kernel ] )
    {
        CMessagebox::GetSingleton().AppendFormat( "Failed to compile ray tracing shader kernel \"%s\".\n", s_KernelDefines[ (int)kernel ] );
        return false;
    }

    return true;
}

void CWavefrontPathTracer::GetBlockDimension( uint32_t* width, uint32_t* height )
{
    uint32_t blockSizeWidth[ s_BlockDimensionCount ] = { 8, 4 };
    uint32_t blockSizeHeight[ s_BlockDimensionCount ] = { 4, 8 };
    *width = blockSizeWidth[ m_BlockDimensionIndex ];
    *height = blockSizeHeight[ m_BlockDimensionIndex ];
}

void CWavefrontPathTracer::RenderOneIteration( const SRenderContext& renderContext, const SRenderData& renderData )
{
    SCOPED_RENDER_ANNOTATION( L"Iteration" );

    ID3D11DeviceContext* deviceContext = GetDeviceContext();

    // Clear the new path & material queue counters
    {
        SCOPED_RENDER_ANNOTATION( L"Clear new path & material queues" );

        uint32_t value[ 4 ] = { 0 };
        deviceContext->ClearUnorderedAccessViewUint( m_QueueCounterBuffers[ 1 ]->GetUAV(), value );
    }

    // Control
    {
        SCOPED_RENDER_ANNOTATION( L"Control" );

        ComputeJob job;
        job.m_ConstantBuffers.push_back( m_ControlConstantBuffer->GetBuffer() );
        job.m_Shader = m_Shaders[ (int)EShaderKernel::Control ].get();
        job.m_SRVs =
        {
              m_RayHitBuffer->GetSRV()
            , m_RayBuffer->GetSRV()
            , m_PixelSampleBuffer->GetSRV()
            , m_MISResultBuffer->GetSRV()
            , m_Scene->m_EnvironmentTexture ? m_Scene->m_EnvironmentTexture->GetSRV() : nullptr
        };
        job.m_UAVs =
        {
              m_PixelPositionBuffer->GetUAV()
            , m_PathAccumulationBuffer->GetUAV()
            , m_FlagsBuffer->GetUAV()
            , m_QueueCounterBuffers[ 1 ]->GetUAV()
            , m_QueueBuffers[ (int)EShaderKernel::Material ]->GetUAV()
            , m_QueueBuffers[ (int)EShaderKernel::NewPath ]->GetUAV()
            , m_NextBlockIndexBuffer->GetUAV()
            , renderData.m_SamplePositionTexture->GetUAV()
            , renderData.m_SampleValueTexture->GetUAV()
        };
        job.m_SamplerStates.push_back( renderData.m_UVClampSamplerState.Get() );
        job.m_DispatchSizeX = CalculateDispatchGroupCount( s_PathPoolLaneCount );
        job.m_DispatchSizeY = 1;
        job.m_DispatchSizeZ = 1;
        job.Dispatch();
    }

    // Copy the new path & material queue counters to constant buffers
    {
        SCOPED_RENDER_ANNOTATION( L"Copy new path & material queue counter" );

        deviceContext->CopyResource( m_QueueConstantsBuffers[ 1 ]->GetBuffer(), m_QueueCounterBuffers[ 1 ]->GetBuffer() );
    }

    // Clear ray counters
    {
        SCOPED_RENDER_ANNOTATION( L"Clear extension & shadow raycast queues" );

        uint32_t value[ 4 ] = { 0 };
        deviceContext->ClearUnorderedAccessViewUint( m_QueueCounterBuffers[ 0 ]->GetUAV(), value );
    }

    // Fill indirect args for new path
    {
        SCOPED_RENDER_ANNOTATION( L"Fill new path indirect arg" );

        ComputeJob job;
        job.m_SRVs.push_back( m_QueueCounterBuffers[ 1 ]->GetSRV( 1, 1 ) );
        job.m_UAVs.push_back( m_IndirectArgumentBuffer[ (int)EShaderKernel::NewPath ]->GetUAV() );
        job.m_Shader = m_Shaders[ (int)EShaderKernel::FillIndirectArguments ].get();
        job.m_DispatchSizeX = 1;
        job.m_DispatchSizeY = 1;
        job.m_DispatchSizeZ = 1;
        job.Dispatch();
    }

    // New Path
    {
        SCOPED_RENDER_ANNOTATION( L"New path" );

        ComputeJob job;
        job.m_ConstantBuffers =
        {
              m_QueueConstantsBuffers[ 1 ]->GetBuffer()
            , renderData.m_RayTracingFrameConstantBuffer->GetBuffer()
            , m_NewPathConstantBuffer->GetBuffer()
        };
        job.m_Shader = m_Shaders[ (int)EShaderKernel::NewPath ].get();
        job.m_SRVs =
        {
              m_QueueBuffers[ (int)EShaderKernel::NewPath ]->GetSRV()
            , m_PixelPositionBuffer->GetSRV()
        };
        job.m_UAVs =
        {
              m_RayBuffer->GetUAV()
            , m_PixelSampleBuffer->GetUAV()
            , m_MISResultBuffer->GetUAV()
            , m_RngBuffer->GetUAV()
            , m_QueueBuffers[ (int)EShaderKernel::ExtensionRayCast ]->GetUAV()
            , m_QueueCounterBuffers[ 0 ]->GetUAV()
        };
        job.DispatchIndirect( m_IndirectArgumentBuffer[ (int)EShaderKernel::NewPath ]->GetBuffer() );
    }

    // Fill indirect args for material
    {
        SCOPED_RENDER_ANNOTATION( L"Fill material indirect arg" );

        ComputeJob job;
        job.m_SRVs.push_back( m_QueueCounterBuffers[ 1 ]->GetSRV( 0, 1 ) );
        job.m_UAVs.push_back( m_IndirectArgumentBuffer[ (int)EShaderKernel::Material ]->GetUAV() );
        job.m_Shader = m_Shaders[ (int)EShaderKernel::FillIndirectArguments ].get();
        job.m_DispatchSizeX = 1;
        job.m_DispatchSizeY = 1;
        job.m_DispatchSizeZ = 1;
        job.Dispatch();
    }

    // Material
    {
        SCOPED_RENDER_ANNOTATION( L"Material" );

        ComputeJob job;
        job.m_ConstantBuffers =
        {
              m_QueueConstantsBuffers[ 1 ]->GetBuffer()
            , m_MaterialConstantBuffer->GetBuffer()
        };
        job.m_Shader = m_Shaders[ (int)EShaderKernel::Material ].get();
        job.m_SRVs =
        {
              m_QueueBuffers[ (int)EShaderKernel::Material ]->GetSRV()
            , m_RayHitBuffer->GetSRV()
            , m_Scene->m_VerticesBuffer->GetSRV()
            , m_Scene->m_TrianglesBuffer->GetSRV()
            , m_Scene->m_LightsBuffer->GetSRV()
            , m_Scene->m_InstanceTransformsBuffer->GetSRV( 0, (uint32_t)m_Scene->m_InstanceTransforms.size() )
            , m_Scene->m_MaterialIdsBuffer->GetSRV()
            , m_Scene->m_MaterialsBuffer->GetSRV()
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
        };
        job.m_UAVs =
        {
              m_RayBuffer->GetUAV()
            , m_ShadowRayBuffer->GetUAV()
            , m_RngBuffer->GetUAV()
            , m_MISResultBuffer->GetUAV()
            , m_PathAccumulationBuffer->GetUAV()
            , m_QueueCounterBuffers[ 0 ]->GetUAV()
            , m_QueueBuffers[ (int)EShaderKernel::ExtensionRayCast ]->GetUAV()
            , m_QueueBuffers[ (int)EShaderKernel::ShadowRayCast ]->GetUAV()
        };
        job.DispatchIndirect( m_IndirectArgumentBuffer[ (int)EShaderKernel::Material ]->GetBuffer() );
    }

    // Copy the extension ray & shadow ray queue counters to constant buffers
    {
        SCOPED_RENDER_ANNOTATION( L"Copy extension & shadow raycast queue counters" );

        deviceContext->CopyResource( m_QueueConstantsBuffers[ 0 ]->GetBuffer(), m_QueueCounterBuffers[ 0 ]->GetBuffer() );
    }

    // Fill indirect args for extension raycast
    {
        SCOPED_RENDER_ANNOTATION( L"Fill extension raycast indirect arg" );

        ComputeJob job;
        job.m_SRVs.push_back( m_QueueCounterBuffers[ 0 ]->GetSRV( 0, 1 ) );
        job.m_UAVs.push_back( m_IndirectArgumentBuffer[ (int)EShaderKernel::ExtensionRayCast ]->GetUAV() );
        job.m_Shader = m_Shaders[ (int)EShaderKernel::FillIndirectArguments ].get();
        job.m_DispatchSizeX = 1;
        job.m_DispatchSizeY = 1;
        job.m_DispatchSizeZ = 1;
        job.Dispatch();
    }

    // Extension raycast
    {
        SCOPED_RENDER_ANNOTATION( L"Extension raycast" );

        ComputeJob job;
        job.m_ConstantBuffers = { m_QueueConstantsBuffers[ 0 ]->GetBuffer() };
        job.m_SRVs =
        {
              m_Scene->m_VerticesBuffer->GetSRV()
            , m_Scene->m_TrianglesBuffer->GetSRV()
            , m_Scene->m_BVHNodesBuffer->GetSRV()
            , m_RayBuffer->GetSRV()
            , m_QueueBuffers[ (int)EShaderKernel::ExtensionRayCast ]->GetSRV()
            , m_Scene->m_InstanceTransformsBuffer->GetSRV( (uint32_t)m_Scene->m_InstanceTransforms.size(), (uint32_t)m_Scene->m_InstanceTransforms.size() )
        };
        job.m_UAVs.push_back( m_RayHitBuffer->GetUAV() );
        job.m_Shader = m_Shaders[ (int)EShaderKernel::ExtensionRayCast ].get();
        job.DispatchIndirect( m_IndirectArgumentBuffer[ (int)EShaderKernel::ExtensionRayCast ]->GetBuffer() );
    }

    // Fill indirect args for shadow raycast
    {
        SCOPED_RENDER_ANNOTATION( L"Fill shadow raycast indirect arg" );

        ComputeJob job;
        job.m_SRVs.push_back( m_QueueCounterBuffers[ 0 ]->GetSRV( 1, 1 ) );
        job.m_UAVs.push_back( m_IndirectArgumentBuffer[ (int)EShaderKernel::ShadowRayCast ]->GetUAV() );
        job.m_Shader = m_Shaders[ (int)EShaderKernel::FillIndirectArguments ].get();
        job.m_DispatchSizeX = 1;
        job.m_DispatchSizeY = 1;
        job.m_DispatchSizeZ = 1;
        job.Dispatch();
    }

    // Shadow raycast
    {
        SCOPED_RENDER_ANNOTATION( L"Shadow raycast" );

        ComputeJob job;
        job.m_ConstantBuffers = { m_QueueConstantsBuffers[ 0 ]->GetBuffer() };
        job.m_SRVs =
        {
              m_Scene->m_VerticesBuffer->GetSRV()
            , m_Scene->m_TrianglesBuffer->GetSRV()
            , m_Scene->m_BVHNodesBuffer->GetSRV()
            , m_ShadowRayBuffer->GetSRV()
            , m_QueueBuffers[ (int)EShaderKernel::ShadowRayCast ]->GetSRV()
            , m_Scene->m_InstanceTransformsBuffer->GetSRV( (uint32_t)m_Scene->m_InstanceTransforms.size(), (uint32_t)m_Scene->m_InstanceTransforms.size() )
        };
        job.m_UAVs.push_back( m_FlagsBuffer->GetUAV() );
        job.m_Shader = m_Shaders[ (int)EShaderKernel::ShadowRayCast ].get();
        job.DispatchIndirect( m_IndirectArgumentBuffer[ (int)EShaderKernel::ShadowRayCast ]->GetBuffer() );
    }
}
