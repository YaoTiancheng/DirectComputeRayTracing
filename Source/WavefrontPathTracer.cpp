#include "stdafx.h"
#include "WavefrontPathTracer.h"
#include "Scene.h"
#include "Shader.h"
#include "GPUBuffer.h"
#include "MessageBox.h"
#include "D3D11RenderSystem.h"
#include "ComputeJob.h"
#include "RenderContext.h"

using namespace DirectX;

static const uint32_t s_WavefrontSize = 32;
static const uint32_t s_PathPoolSize = 512;
static const uint32_t s_PathPoolLaneCount = s_PathPoolSize * s_WavefrontSize;
static const uint32_t s_KernelGroupSize = 32;

struct SControlConstants
{
    XMFLOAT4 g_Background;
    uint32_t g_PathCount;
    uint32_t g_MaxBounceCount;
    uint32_t g_BlockCounts[ 2 ];
    uint32_t g_BlockDimension[ 2 ];
    uint32_t g_FilmDimension[ 2 ];
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
          s_PathPoolLaneCount * 16
        , 16
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

    m_ShadowRayHitBuffer.reset( GPUBuffer::CreateStructured(
          s_PathPoolLaneCount * 4
        , 4
        , D3D11_USAGE_DEFAULT
        , D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS ) );
    if ( !m_ShadowRayHitBuffer )
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

    m_PathThroughputBuffer.reset( GPUBuffer::CreateStructured(
          s_PathPoolLaneCount * 12
        , 12
        , D3D11_USAGE_DEFAULT
        , D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS ) );
    if ( !m_PathThroughputBuffer )
        return false;

    m_LiBuffer.reset( GPUBuffer::CreateStructured(
          s_PathPoolLaneCount * 12
        , 12
        , D3D11_USAGE_DEFAULT
        , D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS ) );
    if ( !m_LiBuffer )
        return false;

    m_BounceBuffer.reset( GPUBuffer::CreateStructured(
          s_PathPoolLaneCount * 4
        , 4
        , D3D11_USAGE_DEFAULT
        , D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS ) );
    if ( !m_BounceBuffer )
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
            , D3D11_BIND_UNORDERED_ACCESS ) );
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

    m_ControlConstantBuffer.reset( GPUBuffer::Create(
          sizeof( SControlConstants )
        , 0
        , DXGI_FORMAT_UNKNOWN
        , D3D11_USAGE_DYNAMIC
        , D3D11_BIND_CONSTANT_BUFFER
        , GPUResourceCreationFlags_CPUWriteable ) );
    if ( !m_ControlConstantBuffer )
        return false;

    return true;
}

void CWavefrontPathTracer::Destroy()
{
    m_RayBuffer.reset();
    m_RayHitBuffer.reset();
    m_ShadowRayBuffer.reset();
    m_ShadowRayHitBuffer.reset();
    m_PixelPositionBuffer.reset();
    m_PixelSampleBuffer.reset();
    m_RngBuffer.reset();
    m_MISResultBuffer.reset();
    m_PathThroughputBuffer.reset();
    m_LiBuffer.reset();
    m_BounceBuffer.reset();
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

    m_ControlConstantBuffer.reset();
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

    if ( m_NewFrame )
    {
        // Clear the nextblockindex
        uint32_t value[ 4 ] = { 0 };
        deviceContext->ClearUnorderedAccessViewUint( m_NextBlockIndexBuffer->GetUAV(), value );

        // Reset all path to idle
        ComputeJob job;
        job.m_ConstantBuffers.push_back( m_ControlConstantBuffer->GetBuffer() );
        job.m_Shader = m_Shaders[ (int)EShaderKernel::SetIdle ].get();
        job.m_UAVs.push_back( m_BounceBuffer->GetUAV() );
        job.m_DispatchSizeX = CalculateDispatchGroupCount( s_PathPoolLaneCount );
        job.m_DispatchSizeY = 1;
        job.m_DispatchSizeZ = 1;
        job.Dispatch();
    }
}

void CWavefrontPathTracer::ResetFrame()
{
    m_NewFrame = true;
}

bool CWavefrontPathTracer::IsFrameComplete()
{
    return false;
}

void CWavefrontPathTracer::OnImGUI()
{
}

bool CWavefrontPathTracer::AcquireFilmClearTrigger()
{
    return false;
}

bool CWavefrontPathTracer::CompileAndCreateShader( EShaderKernel kernel )
{
    std::vector<D3D_SHADER_MACRO> rayTracingShaderDefines;

    static const uint32_t s_MaxRadix10IntegerBufferLengh = 12;
    char buffer_TraversalStackSize[ s_MaxRadix10IntegerBufferLengh ];
    _itoa( m_Scene->m_BVHTraversalStackSize, buffer_TraversalStackSize, 10 );

    rayTracingShaderDefines.push_back( { "RT_BVH_TRAVERSAL_STACK_SIZE", buffer_TraversalStackSize } );

    if ( m_Scene->m_IsBVHDisabled )
    {
        rayTracingShaderDefines.push_back( { "NO_BVH_ACCEL", "0" } );
    }
    if ( m_Scene->m_IsMultipleImportanceSamplingEnabled )
    {
        rayTracingShaderDefines.push_back( { "MULTIPLE_IMPORTANCE_SAMPLING", "0" } );
    }
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
    *width = 8;
    *height = 4;
}
