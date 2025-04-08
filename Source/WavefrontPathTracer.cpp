#include "stdafx.h"
#include "WavefrontPathTracer.h"
#include "D3D12Adapter.h"
#include "D3D12Resource.h"
#include "D3D12GPUDescriptorHeap.h"
#include "D3D12DescriptorUtil.h"
#include "DirectComputeRayTracing.h"
#include "Shader.h"
#include "GPUBuffer.h"
#include "GPUTexture.h"
#include "MessageBox.h"
#include "RenderContext.h"
#include "ScopedRenderAnnotation.h"
#include "Logging.h"
#include "imgui/imgui.h"
#include "../Shaders/LightSharedDef.inc.hlsl"

using namespace DirectX;
using namespace D3D12Util;

enum EBarrierMode : uint32_t
{
    BarrierMode_BeforeUse = 0, BarrierMode_AfterUse = 1, BarrierMode_Split = 2
};

static const uint32_t s_WavefrontSize = 32;
static const uint32_t s_PathPoolSize = 8192;
static const uint32_t s_PathPoolLaneCount = s_PathPoolSize * s_WavefrontSize;
static const uint32_t s_KernelGroupSize = 32;

static const uint32_t s_BlockDimensionCount = 2;

static SD3D12DescriptorTableLayout s_DescriptorTableLayout = SD3D12DescriptorTableLayout( 16, 9 );

struct alignas( 256 ) SControlConstants
{
    XMFLOAT4 g_Background;
    uint32_t g_PathCount;
    uint32_t g_MaxBounceCount;
    uint32_t g_BlockCounts[ 2 ];
    uint32_t g_BlockDimension[ 2 ];
    uint32_t g_FilmDimension[ 2 ];
};

struct alignas( 256 ) SNewPathConstants
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
    uint32_t g_FrameSeed;
};

struct alignas( 256 ) SMaterialConstants
{
    uint32_t g_LightCount;
    uint32_t g_MaxBounceCount;
    uint32_t g_EnvironmentLightIndex;
};

CWavefrontPathTracer::CWavefrontPathTracer()
    : m_BarrierMode( BarrierMode_AfterUse )
{
}

bool CWavefrontPathTracer::Create()
{
    // Static samplers
    D3D12_STATIC_SAMPLER_DESC samplers[ 2 ] = {};
    for ( D3D12_STATIC_SAMPLER_DESC& sampler : samplers )
    {
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.MaxAnisotropy = 1;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    // UVClamp
    samplers[ 0 ].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[ 0 ].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[ 0 ].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[ 0 ].ShaderRegister = 0U;
    // UVWrap
    samplers[ 1 ].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[ 1 ].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[ 1 ].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[ 1 ].ShaderRegister = 1U;

    CD3DX12_ROOT_PARAMETER1 rootParameters[ 2 ];
    rootParameters[ 0 ].InitAsConstantBufferView( 0 );
    SD3D12DescriptorTableRanges descriptorTableRanges;
    s_DescriptorTableLayout.InitRootParameter( &rootParameters[ 1 ], &descriptorTableRanges );

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc( 2, rootParameters, (uint32_t)ARRAY_LENGTH( samplers ), samplers );

    ComPtr<ID3DBlob> serializedRootSignature;
    ComPtr<ID3DBlob> error;
    HRESULT hr = D3D12SerializeVersionedRootSignature( &rootSignatureDesc, serializedRootSignature.GetAddressOf(), error.GetAddressOf() );
    if ( error )
    {
        LOG_STRING_FORMAT( "Create wavefront path tracing root signature with error: %s\n", (const char*)error->GetBufferPointer() );
    }
    if ( FAILED( hr ) )
    {
        return false;
    }

    ID3D12RootSignature* rootSignature = nullptr;
    if ( FAILED( D3D12Adapter::GetDevice()->CreateRootSignature( 0, serializedRootSignature->GetBufferPointer(), serializedRootSignature->GetBufferSize(), IID_PPV_ARGS( &rootSignature ) ) ) )
    {
        return false;
    }
    m_RootSignature.Reset( rootSignature );

    m_RayBuffer.Reset( GPUBuffer::CreateStructured(
          s_PathPoolLaneCount * 32
        , 32
        , EGPUBufferUsage::Default
        , EGPUBufferBindFlag_ShaderResource | EGPUBufferBindFlag_UnorderedAccess ) );
    if ( !m_RayBuffer )
        return false;

    m_RayHitBuffer.Reset( GPUBuffer::CreateStructured(
          s_PathPoolLaneCount * 20
        , 20
        , EGPUBufferUsage::Default
        , EGPUBufferBindFlag_ShaderResource | EGPUBufferBindFlag_UnorderedAccess ) );
    if ( !m_RayHitBuffer )
        return false;

    m_ShadowRayBuffer.Reset( GPUBuffer::CreateStructured(
          s_PathPoolLaneCount * 32
        , 32
        , EGPUBufferUsage::Default
        , EGPUBufferBindFlag_ShaderResource | EGPUBufferBindFlag_UnorderedAccess ) );
    if ( !m_ShadowRayBuffer )
        return false;

    m_PixelPositionBuffer.Reset( GPUBuffer::CreateStructured(
          s_PathPoolLaneCount * 8
        , 8
        , EGPUBufferUsage::Default
        , EGPUBufferBindFlag_ShaderResource | EGPUBufferBindFlag_UnorderedAccess ) );
    if ( !m_PixelPositionBuffer )
        return false;

    m_PixelSampleBuffer.Reset( GPUBuffer::CreateStructured(
          s_PathPoolLaneCount * 8
        , 8
        , EGPUBufferUsage::Default
        , EGPUBufferBindFlag_ShaderResource | EGPUBufferBindFlag_UnorderedAccess ) );
    if ( !m_PixelSampleBuffer )
        return false;

    m_RngBuffer.Reset( GPUBuffer::CreateStructured(
          s_PathPoolLaneCount * 16
        , 16
        , EGPUBufferUsage::Default
        , EGPUBufferBindFlag_ShaderResource | EGPUBufferBindFlag_UnorderedAccess ) );
    if ( !m_RngBuffer )
        return false;

    m_LightSamplingResultsBuffer.Reset( GPUBuffer::Create(
          s_PathPoolLaneCount * 16
        , 16
        , DXGI_FORMAT_R32G32B32A32_FLOAT
        , EGPUBufferUsage::Default
        , EGPUBufferBindFlag_ShaderResource | EGPUBufferBindFlag_UnorderedAccess ) );
    if ( !m_LightSamplingResultsBuffer )
        return false;

    m_PathAccumulationBuffer.Reset( GPUBuffer::CreateStructured(
          s_PathPoolLaneCount * 32
        , 32
        , EGPUBufferUsage::Default
        , EGPUBufferBindFlag_ShaderResource | EGPUBufferBindFlag_UnorderedAccess ) );
    if ( !m_PathAccumulationBuffer )
        return false;

    m_FlagsBuffer.Reset( GPUBuffer::Create(
          s_PathPoolLaneCount * 4
        , 4
        , DXGI_FORMAT_R32_UINT
        , EGPUBufferUsage::Default
        , EGPUBufferBindFlag_ShaderResource | EGPUBufferBindFlag_UnorderedAccess ) );
    if ( !m_FlagsBuffer )
        return false;

    m_NextBlockIndexBuffer.Reset( GPUBuffer::Create(
          4
        , 4
        , DXGI_FORMAT_R32_UINT
        , EGPUBufferUsage::Default
        , EGPUBufferBindFlag_UnorderedAccess ) );
    if ( !m_NextBlockIndexBuffer )
        return false;

    for ( uint32_t i = 0; i < 4; ++i )
    {
        m_IndirectArgumentBuffer[ i ].Reset( GPUBuffer::Create(
              12
            , 4
            , DXGI_FORMAT_R32_UINT
            , EGPUBufferUsage::Default
            , EGPUBufferBindFlag_UnorderedAccess ) );
        if ( !m_IndirectArgumentBuffer[ i ] )
            return false;

        m_QueueBuffers[ i ].Reset( GPUBuffer::Create(
              sizeof( uint32_t ) * s_PathPoolLaneCount
            , sizeof( uint32_t )
            , DXGI_FORMAT_R32_UINT
            , EGPUBufferUsage::Default
            , EGPUBufferBindFlag_ShaderResource | EGPUBufferBindFlag_UnorderedAccess ) );
        if ( !m_QueueBuffers[ i ] )
            return false;
    }

    for ( uint32_t i = 0; i < 2; ++i )
    {
        m_QueueCounterBuffers[ i ].Reset( GPUBuffer::Create(
              8
            , 4
            , DXGI_FORMAT_R32_UINT
            , EGPUBufferUsage::Default
            , EGPUBufferBindFlag_ShaderResource | EGPUBufferBindFlag_UnorderedAccess ) );
        if ( !m_QueueCounterBuffers[ i ] )
            return false;
    }

    for ( uint32_t i = 0; i < s_QueueCounterStagingBufferCount; ++i )
    {
        m_QueueCounterStagingBuffer[ i ].Reset( GPUBuffer::Create(
              16
            , 4
            , DXGI_FORMAT_R32_UINT
            , EGPUBufferUsage::Staging
            , 0 ) );
        if ( !m_QueueCounterStagingBuffer[ i ] )
            return false;
    }

    m_ControlConstantBuffer.Reset( GPUBuffer::Create(
          sizeof( SControlConstants )
        , 0
        , DXGI_FORMAT_UNKNOWN
        , EGPUBufferUsage::Default
        , EGPUBufferBindFlag_ConstantBuffer ) );
    if ( !m_ControlConstantBuffer )
        return false;

    m_NewPathConstantBuffer.Reset( GPUBuffer::Create(
          sizeof( SNewPathConstants )
        , 0
        , DXGI_FORMAT_UNKNOWN
        , EGPUBufferUsage::Default
        , EGPUBufferBindFlag_ConstantBuffer ) );
    if ( !m_NewPathConstantBuffer )
        return false;

    m_MaterialConstantBuffer.Reset( GPUBuffer::Create(
          sizeof( SMaterialConstants )
        , 0
        , DXGI_FORMAT_UNKNOWN
        , EGPUBufferUsage::Default
        , EGPUBufferBindFlag_ConstantBuffer ) );
    if ( !m_MaterialConstantBuffer )
        return false;

    if ( s_QueueCounterStagingBufferCount <= D3D12Adapter::GetBackbufferCount() )
    {
        // We have maximum of "backbuffercount" number of frames in flight, the number of read back staging buffer must be at least backbuffercount + 1
        return false;
    }

    return true;
}

void CWavefrontPathTracer::Destroy()
{
    m_RayBuffer.Reset();
    m_RayHitBuffer.Reset();
    m_ShadowRayBuffer.Reset();
    m_PixelPositionBuffer.Reset();
    m_PixelSampleBuffer.Reset();
    m_RngBuffer.Reset();
    m_LightSamplingResultsBuffer.Reset();
    m_PathAccumulationBuffer.Reset();
    m_FlagsBuffer.Reset();
    m_NextBlockIndexBuffer.Reset();

    for ( uint32_t i = 0; i < 4; ++i )
    {
        m_IndirectArgumentBuffer[ i ].Reset();
        m_QueueBuffers[ i ].Reset();
    }

    for ( uint32_t i = 0; i < 2; ++i )
    {
        m_QueueCounterBuffers[ i ].Reset();
    }

    for ( uint32_t i = 0; i < s_QueueCounterStagingBufferCount; ++i )
    {
        m_QueueCounterStagingBuffer[ i ].Reset();
    }

    m_ControlConstantBuffer.Reset();
    m_NewPathConstantBuffer.Reset();
    m_MaterialConstantBuffer.Reset();

    for ( uint32_t i = 0; i < (uint32_t)EShaderKernel::_Count; ++i )
    {
        m_PSOs[ i ].Reset();
    }

    m_RootSignature.Reset();
}

void CWavefrontPathTracer::OnSceneLoaded( SRenderer* renderer )
{
    for ( int i = 0; i < (int)EShaderKernel::_Count; ++i )
    {
        CompileAndCreateShader( &renderer->m_Scene, (EShaderKernel)i );
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

void CWavefrontPathTracer::Render( SRenderer* renderer, const SRenderContext& renderContext )
{
    CScene* scene = &renderer->m_Scene;
    SBxDFTextures* BxDFTextures = &renderer->m_BxDFTextures;

    uint32_t blockWidth, blockHeight;
    GetBlockDimension( &blockWidth, &blockHeight );

    GPUBuffer::SUploadContext constantBufferUploadContexts[ 3 ]; 

    // Fill control constants buffer
    if ( m_ControlConstantBuffer->AllocateUploadContext( &constantBufferUploadContexts[ 0 ] ) )
    {
        SControlConstants* constants = (SControlConstants*)constantBufferUploadContexts[ 0 ].Map();
        if ( constants )
        {
            constants->g_PathCount = s_PathPoolLaneCount;
            constants->g_MaxBounceCount = scene->m_MaxBounceCount;
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
            constantBufferUploadContexts[ 0 ].Unmap();
        }
    }

    // Fill new path constants buffer
    if ( m_NewPathConstantBuffer->AllocateUploadContext( &constantBufferUploadContexts[ 1 ] ) )
    { 
        SNewPathConstants* constants = (SNewPathConstants*)constantBufferUploadContexts[ 1 ].Map();
        if ( constants )
        {
            scene->m_Camera.GetTransformMatrix( &constants->g_CameraTransform );
            constants->g_Resolution[ 0 ] = renderContext.m_CurrentResolutionWidth;
            constants->g_Resolution[ 1 ] = renderContext.m_CurrentResolutionHeight;
            constants->g_FilmSize = scene->m_FilmSize;
            constants->g_ApertureRadius = scene->CalculateApertureDiameter() * 0.5f;
            constants->g_FocalDistance = scene->m_FocalDistance;
            constants->g_FilmDistance = scene->CalculateFilmDistance();
            constants->g_BladeCount = scene->m_ApertureBladeCount;
            float halfBladeAngle = DirectX::XM_PI / scene->m_ApertureBladeCount;
            constants->g_BladeVertexPos.x = cosf( halfBladeAngle ) * constants->g_ApertureRadius;
            constants->g_BladeVertexPos.y = sinf( halfBladeAngle ) * constants->g_ApertureRadius;
            constants->g_ApertureBaseAngle = scene->m_ApertureRotation;
            constants->g_FrameSeed = renderer->m_FrameSeed;
            constantBufferUploadContexts[ 1 ].Unmap();
        }
    }

    // Fill material constants buffer
    if ( m_MaterialConstantBuffer->AllocateUploadContext( &constantBufferUploadContexts[ 2 ] ) )
    { 
        SMaterialConstants* constants = (SMaterialConstants*)constantBufferUploadContexts[ 2 ].Map();
        if ( constants )
        {
            constants->g_LightCount = scene->GetLightCount();
            constants->g_MaxBounceCount = scene->m_MaxBounceCount;
            constants->g_EnvironmentLightIndex = scene->m_EnvironmentLight ? (uint32_t)scene->m_MeshLights.size() : LIGHT_INDEX_INVALID; // Environment light is right after the mesh lights.
            constantBufferUploadContexts[ 2 ].Unmap();
        }
    }

    ID3D12GraphicsCommandList* commandList = D3D12Adapter::GetCommandList();

    for ( auto& context : constantBufferUploadContexts )
    {
        context.Upload();
    }

    commandList->SetComputeRootSignature( m_RootSignature.Get() );

    CD3D12GPUDescriptorHeap* GPUDescriptorHeap = D3D12Adapter::GetGPUDescriptorHeap();

    if ( m_NewImage )
    {
        SCOPED_RENDER_ANNOTATION( commandList, L"Reset" );

        {
            SD3D12GPUDescriptorHeapHandle nextBlockIndexDesciptor = GPUDescriptorHeap->Allocate( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
            D3D12Adapter::GetDevice()->CopyDescriptorsSimple( 1, nextBlockIndexDesciptor.m_CPU, m_NextBlockIndexBuffer->GetUAV().CPU, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

            // Clear the nextblockindex
            uint32_t value[ 4 ] = { 0 };
            commandList->ClearUnorderedAccessViewUint( nextBlockIndexDesciptor.m_GPU, m_NextBlockIndexBuffer->GetUAV().CPU, m_NextBlockIndexBuffer->GetBuffer(), value, 0, nullptr );
        }

        {
            D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition( m_ControlConstantBuffer->GetBuffer(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER );
            commandList->ResourceBarrier( 1, &barrier );
        }

        {
            D3D12_GPU_DESCRIPTOR_HANDLE descriptorTable = s_DescriptorTableLayout.AllocateAndCopyToDescriptorTable( nullptr, 0, &m_FlagsBuffer->GetUAV(), 1 );
            commandList->SetComputeRootDescriptorTable( 1, descriptorTable );

            commandList->SetComputeRootConstantBufferView( 0, m_ControlConstantBuffer->GetGPUVirtualAddress() );
            commandList->SetPipelineState( m_PSOs[ (int)EShaderKernel::SetIdle ].Get() );
            // Reset all path to idle
            commandList->Dispatch( CalculateDispatchGroupCount( s_PathPoolLaneCount ), 1, 1 );
        }
    }

    for ( uint32_t i = 0; i < m_IterationPerFrame; ++i )
    {
        RenderOneIteration( scene, *BxDFTextures, i == 0 );
    }

    // Copy ray counter to the staging buffer
    {
        SCOPED_RENDER_ANNOTATION( commandList, L"Copy counters to staging buffer" );

        if ( m_NewImage )
        {
            // For new image, initialize all staging buffer
            for ( uint32_t i = 0; i < s_QueueCounterStagingBufferCount; ++i )
            {
                commandList->CopyBufferRegion( m_QueueCounterStagingBuffer[ i ]->GetBuffer(), 0, m_QueueCounterBuffers[ 1 ]->GetBuffer(), 0, 8 );
                m_QueueCounterStagingBufferIndex = 0;
            }
            m_StagingBufferReadyCountdown = D3D12Adapter::GetBackbufferCount();
        }
        else
        {
            commandList->CopyBufferRegion( m_QueueCounterStagingBuffer[ m_QueueCounterStagingBufferIndex ]->GetBuffer(), 0, m_QueueCounterBuffers[ 1 ]->GetBuffer(), 0, 8 );
            m_QueueCounterStagingBufferIndex = ( m_QueueCounterStagingBufferIndex + 1 ) % s_QueueCounterStagingBufferCount;
            if ( m_StagingBufferReadyCountdown > 0 )
            {
                m_StagingBufferReadyCountdown--;
            }
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
    if ( m_StagingBufferReadyCountdown == 0 )
    {
        if ( void* address = m_QueueCounterStagingBuffer[ m_QueueCounterStagingBufferIndex ]->Map() )
        {
            uint32_t* counters = (uint32_t*)address;
            const uint32_t materialQueueCounter = counters[ 0 ];
            const uint32_t newPathQueueCounter = counters[ 1 ];
            const bool areAllQueuesEmpty = !materialQueueCounter && !newPathQueueCounter; // We're done rendering an image if both material and new path queues were drain
            m_QueueCounterStagingBuffer[ m_QueueCounterStagingBufferIndex ]->Unmap();
            return areAllQueuesEmpty;
        }
    }
    return false;
}

void CWavefrontPathTracer::OnImGUI( SRenderer* renderer )
{
    if ( ImGui::CollapsingHeader( "Wavefront Path Tracer" ) )
    {
        ImGui::DragInt( "Iteration Per-frame", (int*)&m_IterationPerFrame, 1.0f, 1, 999, "%d", ImGuiSliderFlags_AlwaysClamp );

        const char* blockDimensionNames[] = { "8x4", "4x8" };
        if ( ImGui::Combo( "Block Dimension", (int*)&m_BlockDimensionIndex, blockDimensionNames, IM_ARRAYSIZE( blockDimensionNames ) ) )
        {
            m_FilmClearTrigger = true;
        }

        const char* barrierModeNames[] = { "BeforeUse", "AfterUse", "Split" };
        if ( ImGui::Combo( "Barrier Mode", (int*)&m_BarrierMode, barrierModeNames, IM_ARRAYSIZE( barrierModeNames ) ) )
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

bool CWavefrontPathTracer::CompileAndCreateShader( CScene* scene, EShaderKernel kernel )
{
    std::vector<DxcDefine> rayTracingShaderDefines;

    static const uint32_t s_MaxRadix10IntegerBufferLengh = 12;
    wchar_t buffer_TraversalStackSize[ s_MaxRadix10IntegerBufferLengh ];
    _itow( scene->m_BVHTraversalStackSize, buffer_TraversalStackSize, 10 );

    rayTracingShaderDefines.push_back( { L"RT_BVH_TRAVERSAL_STACK_SIZE", buffer_TraversalStackSize } );

    rayTracingShaderDefines.push_back( { L"RT_BVH_TRAVERSAL_GROUP_SIZE", L"32" } );

    if ( scene->m_IsGGXVNDFSamplingEnabled )
    {
        rayTracingShaderDefines.push_back( { L"GGX_SAMPLE_VNDF", L"0" } );
    }
    if ( !scene->m_TraverseBVHFrontToBack )
    {
        rayTracingShaderDefines.push_back( { L"BVH_NO_FRONT_TO_BACK_TRAVERSAL", L"0" } );
    }
    if ( scene->m_IsLightVisible )
    {
        rayTracingShaderDefines.push_back( { L"LIGHT_VISIBLE", L"0" } );
    }
    if ( scene->m_EnvironmentLight && scene->m_EnvironmentLight->m_Texture )
    {
        rayTracingShaderDefines.push_back( { L"HAS_ENV_TEXTURE", L"0" } );
    }

    const wchar_t* s_KernelDefines[] = { L"EXTENSION_RAY_CAST", L"SHADOW_RAY_CAST", L"NEW_PATH", L"MATERIAL", L"CONTROL", L"FILL_INDIRECT_ARGUMENTS", L"SET_IDLE" };
    rayTracingShaderDefines.push_back( { s_KernelDefines[ (int)kernel ], L"0" } );

    ComputeShaderPtr shader( ComputeShader::CreateFromFile( L"Shaders\\WavefrontPathTracing.hlsl", rayTracingShaderDefines ) );
    if ( !shader )
    {
        return false;
    }

    // Create PSO
    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_RootSignature.Get();
    psoDesc.CS = shader->GetShaderBytecode();
    psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    ID3D12PipelineState* PSO = nullptr;
    if ( FAILED( D3D12Adapter::GetDevice()->CreateComputePipelineState( &psoDesc, IID_PPV_ARGS( &PSO ) ) ) )
    {
        return false;
    }

    m_PSOs[ (uint32_t)kernel ].Reset( PSO );

    return true;
}

void CWavefrontPathTracer::GetBlockDimension( uint32_t* width, uint32_t* height )
{
    uint32_t blockSizeWidth[ s_BlockDimensionCount ] = { 8, 4 };
    uint32_t blockSizeHeight[ s_BlockDimensionCount ] = { 4, 8 };
    *width = blockSizeWidth[ m_BlockDimensionIndex ];
    *height = blockSizeHeight[ m_BlockDimensionIndex ];
}

void CWavefrontPathTracer::RenderOneIteration( CScene* scene, const SBxDFTextures& BxDFTextures, bool isInitialIteration )
{
    D3D12_RESOURCE_BARRIER_FLAGS barrierFlagBegin = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    D3D12_RESOURCE_BARRIER_FLAGS barrierFlagEnd = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    if ( m_BarrierMode == BarrierMode_Split )
    {
        barrierFlagBegin = D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY;
        barrierFlagEnd = D3D12_RESOURCE_BARRIER_FLAG_END_ONLY;
    }

    ID3D12GraphicsCommandList* commandList = D3D12Adapter::GetCommandList();

    SCOPED_RENDER_ANNOTATION( commandList, L"Iteration" );

    CD3D12GPUDescriptorHeap* GPUDescriptorHeap = D3D12Adapter::GetGPUDescriptorHeap();

    // Barriers
    if ( !isInitialIteration )
    {
        D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition( m_QueueCounterBuffers[ 1 ]->GetBuffer(),
            D3D12_RESOURCE_STATE_COPY_SOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
        commandList->ResourceBarrier( 1, &barrier );
    }

    // Clear the new path & material queue counters
    {
        SCOPED_RENDER_ANNOTATION( commandList, L"Clear new path & material queues" );

        GPUBuffer* queueCounterBuffer = m_QueueCounterBuffers[ 1 ].Get();
        SD3D12GPUDescriptorHeapHandle queueCounterDescriptor = GPUDescriptorHeap->Allocate( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
        D3D12Adapter::GetDevice()->CopyDescriptorsSimple( 1, queueCounterDescriptor.m_CPU, queueCounterBuffer->GetUAV().CPU, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

        uint32_t value[ 4 ] = { 0 };
        commandList->ClearUnorderedAccessViewUint( queueCounterDescriptor.m_GPU, queueCounterBuffer->GetUAV().CPU, queueCounterBuffer->GetBuffer(), value, 0, nullptr );
    }

    // Barriers
    {
        std::vector<D3D12_RESOURCE_BARRIER> barriers;
        barriers.reserve( 11 );

        if ( scene->m_IsSampleTexturesRead )
        { 
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( scene->m_SamplePositionTexture->GetTexture(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS ) );
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( scene->m_SampleValueTexture->GetTexture(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS ) );
            scene->m_IsSampleTexturesRead = false;
        }
        barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::UAV( m_QueueCounterBuffers[ 1 ]->GetBuffer() ) );

        // Following barriers are conditional because of implicit state transitions

        if ( !isInitialIteration )
        {
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_PixelSampleBuffer->GetBuffer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE ) );
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_LightSamplingResultsBuffer->GetBuffer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE ) );
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_PixelPositionBuffer->GetBuffer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS ) );
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::UAV( m_PathAccumulationBuffer->GetBuffer() ) );
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_QueueBuffers[ (int)EShaderKernel::Material ]->GetBuffer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS ) );
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_QueueBuffers[ (int)EShaderKernel::NewPath ]->GetBuffer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS ) );
        }
        else if ( !m_NewImage )
        {
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_ControlConstantBuffer->GetBuffer(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER ) );
        }

        if ( m_NewImage || !isInitialIteration )
        {
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::UAV( m_FlagsBuffer->GetBuffer() ) );
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::UAV( m_NextBlockIndexBuffer->GetBuffer() ) );
        }
        
        commandList->ResourceBarrier( (uint32_t)barriers.size(), barriers.data() );
    }

    // Control
    {
        SCOPED_RENDER_ANNOTATION( commandList, L"Control" );

        SD3D12DescriptorHandle SRVs[] =
        {
              m_PixelSampleBuffer->GetSRV()
            , m_LightSamplingResultsBuffer->GetSRV()
        };
        SD3D12DescriptorHandle UAVs[] =
        {
              m_PixelPositionBuffer->GetUAV()
            , m_PathAccumulationBuffer->GetUAV()
            , m_FlagsBuffer->GetUAV()
            , m_QueueCounterBuffers[ 1 ]->GetUAV()
            , m_QueueBuffers[ (int)EShaderKernel::Material ]->GetUAV()
            , m_QueueBuffers[ (int)EShaderKernel::NewPath ]->GetUAV()
            , m_NextBlockIndexBuffer->GetUAV()
            , scene->m_SamplePositionTexture->GetUAV()
            , scene->m_SampleValueTexture->GetUAV()
        };
        D3D12_GPU_DESCRIPTOR_HANDLE descriptorTable = s_DescriptorTableLayout.AllocateAndCopyToDescriptorTable( SRVs, (uint32_t)ARRAY_LENGTH( SRVs ), UAVs, (uint32_t)ARRAY_LENGTH( UAVs ) );

        commandList->SetComputeRootConstantBufferView( 0, m_ControlConstantBuffer->GetGPUVirtualAddress() );
        commandList->SetComputeRootDescriptorTable( 1, descriptorTable );
        commandList->SetPipelineState( m_PSOs[ (int)EShaderKernel::Control ].Get() );
        commandList->Dispatch( CalculateDispatchGroupCount( s_PathPoolLaneCount ), 1, 1 );
    }

    // Barriers
    if ( !isInitialIteration )
    {
        D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition( m_QueueCounterBuffers[ 0 ]->GetBuffer(),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
        commandList->ResourceBarrier( 1, &barrier );
    }

    // Clear ray counters
    {
        SCOPED_RENDER_ANNOTATION( commandList, L"Clear extension & shadow raycast queues" );

        GPUBuffer* queueCounterBuffer = m_QueueCounterBuffers[ 0 ].Get();
        SD3D12GPUDescriptorHeapHandle queueCounterDescriptor = GPUDescriptorHeap->Allocate( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
        D3D12Adapter::GetDevice()->CopyDescriptorsSimple( 1, queueCounterDescriptor.m_CPU, queueCounterBuffer->GetUAV().CPU, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

        uint32_t value[ 4 ] = { 0 };
        commandList->ClearUnorderedAccessViewUint( queueCounterDescriptor.m_GPU, queueCounterBuffer->GetUAV().CPU, queueCounterBuffer->GetBuffer(), value, 0, nullptr );
    }

    // Barriers
    {
        std::vector<D3D12_RESOURCE_BARRIER> barriers;
        barriers.reserve( 2 );

        barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_QueueCounterBuffers[ 1 ]->GetBuffer(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_COPY_SOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE ) );

        if ( !isInitialIteration )
        {
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_IndirectArgumentBuffer[ (int)EShaderKernel::NewPath ]->GetBuffer(),
                D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS ) );
        }

        commandList->ResourceBarrier( (uint32_t)barriers.size(), barriers.data() );
    }

    // Fill indirect args for new path
    {
        SCOPED_RENDER_ANNOTATION( commandList, L"Fill new path indirect arg" );

        SD3D12DescriptorHandle SRV = m_QueueCounterBuffers[ 1 ]->GetSRV( DXGI_FORMAT_R32_UINT, 0, 1, 1 );
        SD3D12DescriptorHandle UAV = m_IndirectArgumentBuffer[ (int)EShaderKernel::NewPath ]->GetUAV();
        D3D12_GPU_DESCRIPTOR_HANDLE descriptorTable = s_DescriptorTableLayout.AllocateAndCopyToDescriptorTable( &SRV, 1, &UAV, 1 );

        commandList->SetComputeRootDescriptorTable( 1, descriptorTable );
        commandList->SetPipelineState( m_PSOs[ (int)EShaderKernel::FillIndirectArguments ].Get() );
        commandList->Dispatch( 1, 1, 1 );
    }

    // Barriers
    if ( !isInitialIteration )
    {
        // Write wait for material read
        D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition( m_IndirectArgumentBuffer[ (int)EShaderKernel::Material ]->GetBuffer(),
            D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
        commandList->ResourceBarrier( 1, &barrier );
    }

    // Fill indirect args for material
    {
        SCOPED_RENDER_ANNOTATION( commandList, L"Fill material indirect arg" );

        SD3D12DescriptorHandle SRV = m_QueueCounterBuffers[ 1 ]->GetSRV( DXGI_FORMAT_R32_UINT, 0, 0, 1 );
        SD3D12DescriptorHandle UAV = m_IndirectArgumentBuffer[ (int)EShaderKernel::Material ]->GetUAV();
        D3D12_GPU_DESCRIPTOR_HANDLE descriptorTable = s_DescriptorTableLayout.AllocateAndCopyToDescriptorTable( &SRV, 1, &UAV, 1 );

        commandList->SetComputeRootDescriptorTable( 1, descriptorTable );
        commandList->SetPipelineState( m_PSOs[ (int)EShaderKernel::FillIndirectArguments ].Get() );
        commandList->Dispatch( 1, 1, 1 );
    }

    // Barriers
    {
        std::vector<D3D12_RESOURCE_BARRIER> barriers;
        barriers.reserve( 9 );

        barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_QueueBuffers[ (int)EShaderKernel::NewPath ]->GetBuffer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE ) );
        barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_PixelPositionBuffer->GetBuffer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE ) );
        barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_PixelSampleBuffer->GetBuffer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS ) );
        barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_LightSamplingResultsBuffer->GetBuffer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS ) );
        barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::UAV( m_QueueCounterBuffers[ 0 ]->GetBuffer() ) );
        barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_IndirectArgumentBuffer[ (int)EShaderKernel::NewPath ]->GetBuffer(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT ) );

        if ( isInitialIteration )
        {
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_NewPathConstantBuffer->GetBuffer(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER ) );
        }
        else
        {
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_RayBuffer->GetBuffer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS ) );
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::UAV( m_RngBuffer->GetBuffer() ) );
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_QueueBuffers[ (int)EShaderKernel::ExtensionRayCast ]->GetBuffer(),
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS ) );
        }

        commandList->ResourceBarrier( (uint32_t)barriers.size(), barriers.data() );
    }

    // New Path
    {
        SCOPED_RENDER_ANNOTATION( commandList, L"New path" );

        SD3D12DescriptorHandle SRVs[] =
        {
              m_QueueBuffers[ (int)EShaderKernel::NewPath ]->GetSRV()
            , m_QueueCounterBuffers[ 1 ]->GetSRV()
            , m_PixelPositionBuffer->GetSRV()
        };
        SD3D12DescriptorHandle UAVs[] =
        {
              m_RayBuffer->GetUAV()
            , m_PixelSampleBuffer->GetUAV()
            , m_LightSamplingResultsBuffer->GetUAV()
            , m_RngBuffer->GetUAV()
            , m_QueueBuffers[ (int)EShaderKernel::ExtensionRayCast ]->GetUAV()
            , m_QueueCounterBuffers[ 0 ]->GetUAV()
        };
        D3D12_GPU_DESCRIPTOR_HANDLE descriptorTable = s_DescriptorTableLayout.AllocateAndCopyToDescriptorTable( SRVs, (uint32_t)ARRAY_LENGTH( SRVs ), UAVs, (uint32_t)ARRAY_LENGTH( UAVs ) );

        commandList->SetComputeRootConstantBufferView( 0, m_NewPathConstantBuffer->GetGPUVirtualAddress() );
        commandList->SetComputeRootDescriptorTable( 1, descriptorTable );
        commandList->SetPipelineState( m_PSOs[ (int)EShaderKernel::NewPath ].Get() );
        commandList->ExecuteIndirect( D3D12Adapter::GetDispatchIndirectCommandSignature(), 1, m_IndirectArgumentBuffer[ (int)EShaderKernel::NewPath ]->GetBuffer(), 0, nullptr, 0 );
    }

    // Barriers
    {
        std::vector<D3D12_RESOURCE_BARRIER> barriers;
        barriers.reserve( 10 );

        // Read wait for control write
        barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_QueueBuffers[ (int)EShaderKernel::Material ]->GetBuffer(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE ) ); 
        // R/W wait for control R/W
        barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::UAV( m_PathAccumulationBuffer->GetBuffer() ) );
        barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::UAV( m_FlagsBuffer->GetBuffer() ) );
        // R/W wait for new path R/W
        barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::UAV( m_QueueCounterBuffers[ 0 ]->GetBuffer() ) );
        barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_IndirectArgumentBuffer[ (int)EShaderKernel::Material ]->GetBuffer(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT ) );

        if ( !scene->m_IsLightBufferRead )
        {
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( scene->m_LightsBuffer->GetBuffer(),
                D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE ) );
            scene->m_IsLightBufferRead = true;
        }
        if ( !scene->m_IsMaterialBufferRead )
        {
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( scene->m_MaterialsBuffer->GetBuffer(),
                D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE ) );
            scene->m_IsMaterialBufferRead = true;
        }

#if 0
        // New path and material write to different indices of following buffers, their UAV barriers are not necessary
        barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::UAV( m_RayBuffer->GetBuffer() ) );
        barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::UAV( m_RngBuffer->GetBuffer() ) );
        barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::UAV( m_LightSamplingResultsBuffer->GetBuffer() ) );
        barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::UAV( m_QueueBuffers[ (int)EShaderKernel::ExtensionRayCast ]->GetBuffer() ) );
#endif
        if ( isInitialIteration )
        {
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_MaterialConstantBuffer->GetBuffer(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER ) );
        }
        else
        {
            // Read wait for extension raycast write
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_RayHitBuffer->GetBuffer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE ) );
            // Write wait for shadow raycast read
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_ShadowRayBuffer->GetBuffer(),
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS ) );
            // Write wait for shadow raycast read
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_QueueBuffers[ (int)EShaderKernel::ShadowRayCast ]->GetBuffer(),
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS ) );
        }

        commandList->ResourceBarrier( (uint32_t)barriers.size(), barriers.data() );
    }

    // Material
    {
        SCOPED_RENDER_ANNOTATION( commandList, L"Material" );

        SD3D12DescriptorHandle environmentTextureSRV = D3D12Adapter::GetNullBufferSRV();
        if ( scene->m_EnvironmentLight && scene->m_EnvironmentLight->m_Texture )
        {
            environmentTextureSRV = scene->m_EnvironmentLight->m_Texture->GetSRV();
        }

        SD3D12DescriptorHandle SRVs[] =
        {
              m_QueueBuffers[ (int)EShaderKernel::Material ]->GetSRV()
            , m_QueueCounterBuffers[ 1 ]->GetSRV()
            , m_RayHitBuffer->GetSRV()
            , scene->m_VerticesBuffer->GetSRV()
            , scene->m_TrianglesBuffer->GetSRV()
            , scene->m_LightsBuffer->GetSRV()
            , scene->m_InstanceTransformsBuffer->GetSRV( DXGI_FORMAT_UNKNOWN, sizeof( XMFLOAT4X3 ), 0, (uint32_t)scene->m_InstanceTransforms.size() )
            , scene->m_MaterialIdsBuffer->GetSRV()
            , scene->m_MaterialsBuffer->GetSRV()
            , scene->m_InstanceLightIndicesBuffer->GetSRV()
            , BxDFTextures.m_CookTorranceBRDF->GetSRV()
            , BxDFTextures.m_CookTorranceBRDFAverage->GetSRV()
            , BxDFTextures.m_CookTorranceBRDFDielectric->GetSRV()
            , BxDFTextures.m_CookTorranceBSDF->GetSRV()
            , BxDFTextures.m_CookTorranceBSDFAverage->GetSRV()
            , environmentTextureSRV
        };

        SD3D12DescriptorHandle UAVs[] =
        {
              m_RayBuffer->GetUAV()
            , m_ShadowRayBuffer->GetUAV()
            , m_FlagsBuffer->GetUAV()
            , m_RngBuffer->GetUAV()
            , m_LightSamplingResultsBuffer->GetUAV()
            , m_PathAccumulationBuffer->GetUAV()
            , m_QueueCounterBuffers[ 0 ]->GetUAV()
            , m_QueueBuffers[ (int)EShaderKernel::ExtensionRayCast ]->GetUAV()
            , m_QueueBuffers[ (int)EShaderKernel::ShadowRayCast ]->GetUAV()
        };

        std::vector<SD3D12DescriptorHandle> textureSRVs;
        textureSRVs.resize( scene->m_GPUTextures.size() );
        scene->CopyTextureDescriptors( textureSRVs.data() );

        D3D12_GPU_DESCRIPTOR_HANDLE descriptorTable = s_DescriptorTableLayout.AllocateAndCopyToDescriptorTable( SRVs, (uint32_t)ARRAY_LENGTH( SRVs ), UAVs, (uint32_t)ARRAY_LENGTH( UAVs ),
            textureSRVs.data(), (uint32_t)textureSRVs.size() );

        commandList->SetComputeRootConstantBufferView( 0, m_MaterialConstantBuffer->GetGPUVirtualAddress() );
        commandList->SetComputeRootDescriptorTable( 1, descriptorTable );
        commandList->SetPipelineState( m_PSOs[ (int)EShaderKernel::Material ].Get() );
        commandList->ExecuteIndirect( D3D12Adapter::GetDispatchIndirectCommandSignature(), 1, m_IndirectArgumentBuffer[ (int)EShaderKernel::Material ]->GetBuffer(), 0, nullptr, 0 );
    }

    // Barrier
    {
        std::vector<D3D12_RESOURCE_BARRIER> barriers;
        barriers.reserve( 8 );

        barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_QueueCounterBuffers[ 0 ]->GetBuffer(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE ) );

        if ( !isInitialIteration )
        {
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_IndirectArgumentBuffer[ (int)EShaderKernel::ExtensionRayCast ]->GetBuffer(),
                D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS ) );
        }

        if ( m_BarrierMode == BarrierMode_AfterUse || m_BarrierMode == BarrierMode_Split )
        {
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_RayBuffer->GetBuffer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, barrierFlagBegin ) );
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_QueueBuffers[ (int)EShaderKernel::ExtensionRayCast ]->GetBuffer(),
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, barrierFlagBegin ) );
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_RayHitBuffer->GetBuffer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, barrierFlagBegin ) );
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_ShadowRayBuffer->GetBuffer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, barrierFlagBegin ) );
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_QueueBuffers[ (int)EShaderKernel::ShadowRayCast ]->GetBuffer(),
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, barrierFlagBegin ) );

            if ( !isInitialIteration )
            {
                barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_IndirectArgumentBuffer[ (int)EShaderKernel::ShadowRayCast ]->GetBuffer(),
                    D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, barrierFlagBegin ) ); // Could begin earlier
            }
        }

        commandList->ResourceBarrier( (uint32_t)barriers.size(), barriers.data() );
    }

    // Fill indirect args for extension raycast
    {
        SCOPED_RENDER_ANNOTATION( commandList, L"Fill extension raycast indirect arg" );

        SD3D12DescriptorHandle SRV = m_QueueCounterBuffers[ 0 ]->GetSRV( DXGI_FORMAT_R32_UINT, 0, 0, 1 );
        SD3D12DescriptorHandle UAV = m_IndirectArgumentBuffer[ (int)EShaderKernel::ExtensionRayCast ]->GetUAV();
        D3D12_GPU_DESCRIPTOR_HANDLE descriptorTable = s_DescriptorTableLayout.AllocateAndCopyToDescriptorTable( &SRV, 1, &UAV, 1 );

        commandList->SetComputeRootDescriptorTable( 1, descriptorTable );
        commandList->SetPipelineState( m_PSOs[ (int)EShaderKernel::FillIndirectArguments ].Get() );
        commandList->Dispatch( 1, 1, 1 );
    }

    // Barriers
    if ( m_BarrierMode == BarrierMode_BeforeUse || m_BarrierMode == BarrierMode_Split )
    { 
        if ( !isInitialIteration )
        {
            D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition( m_IndirectArgumentBuffer[ (int)EShaderKernel::ShadowRayCast ]->GetBuffer(),
                D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, barrierFlagEnd );
            commandList->ResourceBarrier( 1, &barrier );
        }
    }

    // Fill indirect args for shadow raycast
    {
        SCOPED_RENDER_ANNOTATION( commandList, L"Fill shadow raycast indirect arg" );

        SD3D12DescriptorHandle SRV = m_QueueCounterBuffers[ 0 ]->GetSRV( DXGI_FORMAT_R32_UINT, 0, 1, 1 );
        SD3D12DescriptorHandle UAV = m_IndirectArgumentBuffer[ (int)EShaderKernel::ShadowRayCast ]->GetUAV();
        D3D12_GPU_DESCRIPTOR_HANDLE descriptorTable = s_DescriptorTableLayout.AllocateAndCopyToDescriptorTable( &SRV, 1, &UAV, 1 );

        commandList->SetComputeRootDescriptorTable( 1, descriptorTable );
        commandList->SetPipelineState( m_PSOs[ (int)EShaderKernel::FillIndirectArguments ].Get() );
        commandList->Dispatch( 1, 1, 1 );
    }

    // Barriers
    {
        std::vector<D3D12_RESOURCE_BARRIER> barriers;
        barriers.reserve( 5 );

        if ( m_BarrierMode == BarrierMode_BeforeUse || m_BarrierMode == BarrierMode_Split )
        { 
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_RayBuffer->GetBuffer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, barrierFlagEnd ) );
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_QueueBuffers[ (int)EShaderKernel::ExtensionRayCast ]->GetBuffer(),
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, barrierFlagEnd ) );
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_RayHitBuffer->GetBuffer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, barrierFlagEnd ) );
        }
        if ( m_BarrierMode == BarrierMode_AfterUse || m_BarrierMode == BarrierMode_Split )
        {
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_IndirectArgumentBuffer[ (int)EShaderKernel::ShadowRayCast ]->GetBuffer(),
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, barrierFlagBegin ) );
        }
        barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_IndirectArgumentBuffer[ (int)EShaderKernel::ExtensionRayCast ]->GetBuffer(), 
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT ) );

        commandList->ResourceBarrier( (uint32_t)barriers.size(), barriers.data() );
    }

    // Extension raycast
    {
        SCOPED_RENDER_ANNOTATION( commandList, L"Extension raycast" );

        SD3D12DescriptorHandle SRVs[] = 
        {
              scene->m_VerticesBuffer->GetSRV()
            , scene->m_TrianglesBuffer->GetSRV()
            , scene->m_BVHNodesBuffer->GetSRV()
            , m_RayBuffer->GetSRV()
            , m_QueueBuffers[ (int)EShaderKernel::ExtensionRayCast ]->GetSRV()
            , m_QueueCounterBuffers[ 0 ]->GetSRV()
            , scene->m_InstanceTransformsBuffer->GetSRV( DXGI_FORMAT_UNKNOWN, sizeof( XMFLOAT4X3 ), (uint32_t)scene->m_InstanceTransforms.size(), (uint32_t)scene->m_InstanceTransforms.size() )
        };
        SD3D12DescriptorHandle UAVs[] = { m_RayHitBuffer->GetUAV() };
        D3D12_GPU_DESCRIPTOR_HANDLE descriptorTable = s_DescriptorTableLayout.AllocateAndCopyToDescriptorTable( SRVs, (uint32_t)ARRAY_LENGTH( SRVs ), UAVs, (uint32_t)ARRAY_LENGTH( UAVs ) );

        commandList->SetComputeRootDescriptorTable( 1, descriptorTable );
        commandList->SetPipelineState( m_PSOs[ (int)EShaderKernel::ExtensionRayCast ].Get() );
        commandList->ExecuteIndirect( D3D12Adapter::GetDispatchIndirectCommandSignature(), 1, m_IndirectArgumentBuffer[ (int)EShaderKernel::ExtensionRayCast ]->GetBuffer(), 0, nullptr, 0 );
    }

    // Barriers
    {
        std::vector<D3D12_RESOURCE_BARRIER> barriers;
        barriers.reserve( 4 );

        if ( m_BarrierMode == BarrierMode_BeforeUse || m_BarrierMode == BarrierMode_Split )
        { 
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_ShadowRayBuffer->GetBuffer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, barrierFlagEnd ) );
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_QueueBuffers[ (int)EShaderKernel::ShadowRayCast ]->GetBuffer(),
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, barrierFlagEnd ) );
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_IndirectArgumentBuffer[ (int)EShaderKernel::ShadowRayCast ]->GetBuffer(),
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, barrierFlagEnd ) );
        }
#if 0
        barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::UAV( m_FlagsBuffer->GetBuffer() ) );
#endif
        
        if ( !barriers.empty() )
        {
            commandList->ResourceBarrier( (uint32_t)barriers.size(), barriers.data() );
        }
    }

    // Shadow raycast
    {
        SCOPED_RENDER_ANNOTATION( commandList, L"Shadow raycast" );

        SD3D12DescriptorHandle SRVs[] = 
        {
              scene->m_VerticesBuffer->GetSRV()
            , scene->m_TrianglesBuffer->GetSRV()
            , scene->m_BVHNodesBuffer->GetSRV()
            , m_ShadowRayBuffer->GetSRV()
            , m_QueueBuffers[ (int)EShaderKernel::ShadowRayCast ]->GetSRV()
            , m_QueueCounterBuffers[ 0 ]->GetSRV()
            , scene->m_InstanceTransformsBuffer->GetSRV( DXGI_FORMAT_UNKNOWN, sizeof( XMFLOAT4X3 ), (uint32_t)scene->m_InstanceTransforms.size(), (uint32_t)scene->m_InstanceTransforms.size() )
        };
        SD3D12DescriptorHandle UAVs[] = { m_FlagsBuffer->GetUAV() };
        D3D12_GPU_DESCRIPTOR_HANDLE descriptorTable = s_DescriptorTableLayout.AllocateAndCopyToDescriptorTable( SRVs, (uint32_t)ARRAY_LENGTH( SRVs ), UAVs, (uint32_t)ARRAY_LENGTH( UAVs ) );

        commandList->SetComputeRootDescriptorTable( 1, descriptorTable );
        commandList->SetPipelineState( m_PSOs[ (int)EShaderKernel::ShadowRayCast ].Get() );
        commandList->ExecuteIndirect( D3D12Adapter::GetDispatchIndirectCommandSignature(), 1, m_IndirectArgumentBuffer[ (int)EShaderKernel::ShadowRayCast ]->GetBuffer(), 0, nullptr, 0 );
    }
}
