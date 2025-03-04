#include "stdafx.h"
#include "WavefrontPathTracer.h"
#include "D3D12Adapter.h"
#include "D3D12Resource.h"
#include "D3D12GPUDescriptorHeap.h"
#include "D3D12DescriptorUtil.h"
#include "Scene.h"
#include "Shader.h"
#include "GPUBuffer.h"
#include "GPUTexture.h"
#include "MessageBox.h"
#include "RenderContext.h"
#include "BxDFTextures.h"
#include "ScopedRenderAnnotation.h"
#include "imgui/imgui.h"
#include "../Shaders/LightSharedDef.inc.hlsl"

using namespace DirectX;
using namespace D3D12Util;

static const uint32_t s_WavefrontSize = 32;
static const uint32_t s_PathPoolSize = 8192;
static const uint32_t s_PathPoolLaneCount = s_PathPoolSize * s_WavefrontSize;
static const uint32_t s_KernelGroupSize = 32;

static const uint32_t s_BlockDimensionCount = 2;

static SD3D12DescriptorTableLayout s_DescriptorTableLayout = SD3D12DescriptorTableLayout( 15, 9 );

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
    uint32_t g_MaxBounceCount;
    uint32_t g_EnvironmentLightIndex;
    uint32_t padding;
};

bool CWavefrontPathTracer::Create()
{
    // Static sampler
    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxAnisotropy = 1;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    CD3DX12_ROOT_PARAMETER1 rootParameters[ 4 ];
    rootParameters[ 0 ].InitAsConstantBufferView( 0 );
    rootParameters[ 1 ].InitAsConstantBufferView( 1 );
    rootParameters[ 2 ].InitAsConstantBufferView( 2 );
    s_DescriptorTableLayout.InitRootParameter( &rootParameters[ 3 ] );

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc( 4, rootParameters, 1, &sampler );

    ComPtr<ID3DBlob> serializedRootSignature;
    ComPtr<ID3DBlob> error;
    HRESULT hr = D3D12SerializeVersionedRootSignature( &rootSignatureDesc, serializedRootSignature.GetAddressOf(), error.GetAddressOf() );
    LOG_STRING_FORMAT( "Create wavefront path tracing root signature with error: %s\n", (const char*)error->GetBufferPointer() );
    if ( FAILED( hr ) )
    {
        return false;
    }

    if ( FAILED( D3D12Adapter::GetDevice()->CreateRootSignature( 0, serializedRootSignature->GetBufferPointer(), serializedRootSignature->GetBufferSize(), IID_PPV_ARGS( m_RootSignature.GetAddressOf() ) ) ) )
    {
        return false;
    }

    m_RayBuffer.reset( GPUBuffer::CreateStructured(
          s_PathPoolLaneCount * 32
        , 32
        , EGPUBufferUsage::Default
        , EGPUBufferBindFlag_ShaderResource | EGPUBufferBindFlag_UnorderedAccess ) );
    if ( !m_RayBuffer )
        return false;

    m_RayHitBuffer.reset( GPUBuffer::CreateStructured(
          s_PathPoolLaneCount * 20
        , 20
        , EGPUBufferUsage::Default
        , EGPUBufferBindFlag_ShaderResource | EGPUBufferBindFlag_UnorderedAccess ) );
    if ( !m_RayHitBuffer )
        return false;

    m_ShadowRayBuffer.reset( GPUBuffer::CreateStructured(
          s_PathPoolLaneCount * 32
        , 32
        , EGPUBufferUsage::Default
        , EGPUBufferBindFlag_ShaderResource | EGPUBufferBindFlag_UnorderedAccess ) );
    if ( !m_ShadowRayBuffer )
        return false;

    m_PixelPositionBuffer.reset( GPUBuffer::CreateStructured(
          s_PathPoolLaneCount * 8
        , 8
        , EGPUBufferUsage::Default
        , EGPUBufferBindFlag_ShaderResource | EGPUBufferBindFlag_UnorderedAccess ) );
    if ( !m_PixelPositionBuffer )
        return false;

    m_PixelSampleBuffer.reset( GPUBuffer::CreateStructured(
          s_PathPoolLaneCount * 8
        , 8
        , EGPUBufferUsage::Default
        , EGPUBufferBindFlag_ShaderResource | EGPUBufferBindFlag_UnorderedAccess ) );
    if ( !m_PixelSampleBuffer )
        return false;

    m_RngBuffer.reset( GPUBuffer::CreateStructured(
          s_PathPoolLaneCount * 16
        , 16
        , EGPUBufferUsage::Default
        , EGPUBufferBindFlag_ShaderResource | EGPUBufferBindFlag_UnorderedAccess ) );
    if ( !m_RngBuffer )
        return false;

    m_LightSamplingResultsBuffer.reset( GPUBuffer::Create(
          s_PathPoolLaneCount * 16
        , 16
        , DXGI_FORMAT_R32G32B32A32_FLOAT
        , EGPUBufferUsage::Default
        , EGPUBufferBindFlag_ShaderResource | EGPUBufferBindFlag_UnorderedAccess ) );
    if ( !m_LightSamplingResultsBuffer )
        return false;

    m_PathAccumulationBuffer.reset( GPUBuffer::CreateStructured(
          s_PathPoolLaneCount * 32
        , 32
        , EGPUBufferUsage::Default
        , EGPUBufferBindFlag_ShaderResource | EGPUBufferBindFlag_UnorderedAccess ) );
    if ( !m_PathAccumulationBuffer )
        return false;

    m_FlagsBuffer.reset( GPUBuffer::Create(
          s_PathPoolLaneCount * 4
        , 4
        , DXGI_FORMAT_R32_UINT
        , EGPUBufferUsage::Default
        , EGPUBufferBindFlag_ShaderResource | EGPUBufferBindFlag_UnorderedAccess ) );
    if ( !m_FlagsBuffer )
        return false;

    m_NextBlockIndexBuffer.reset( GPUBuffer::Create(
          4
        , 4
        , DXGI_FORMAT_R32_UINT
        , EGPUBufferUsage::Default
        , EGPUBufferBindFlag_UnorderedAccess ) );
    if ( !m_NextBlockIndexBuffer )
        return false;

    for ( uint32_t i = 0; i < 4; ++i )
    {
        m_IndirectArgumentBuffer[ i ].reset( GPUBuffer::Create(
              12
            , 4
            , DXGI_FORMAT_R32_UINT
            , EGPUBufferUsage::Default
            , EGPUBufferBindFlag_UnorderedAccess ) );
        if ( !m_IndirectArgumentBuffer[ i ] )
            return false;

        m_QueueBuffers[ i ].reset( GPUBuffer::Create(
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
        m_QueueCounterBuffers[ i ].reset( GPUBuffer::Create(
              16
            , 4
            , DXGI_FORMAT_R32_UINT
            , EGPUBufferUsage::Default
            , EGPUBufferBindFlag_ShaderResource | EGPUBufferBindFlag_UnorderedAccess ) );
        if ( !m_QueueCounterBuffers[ i ] )
            return false;

        m_QueueConstantsBuffers[ i ].reset( GPUBuffer::Create(
              16
            , 4
            , DXGI_FORMAT_R32_UINT
            , EGPUBufferUsage::Default
            , EGPUBufferBindFlag_ConstantBuffer ) );
        if ( !m_QueueConstantsBuffers[ i ] )
            return false;
    }

    for ( uint32_t i = 0; i < s_QueueCounterStagingBufferCount; ++i )
    {
        m_QueueCounterStagingBuffer[ i ].reset( GPUBuffer::Create(
              16
            , 4
            , DXGI_FORMAT_R32_UINT
            , EGPUBufferUsage::Staging
            , 0 ) );
        if ( !m_QueueCounterStagingBuffer[ i ] )
            return false;
    }

    m_ControlConstantBuffer.reset( GPUBuffer::Create(
          sizeof( SControlConstants )
        , 0
        , DXGI_FORMAT_UNKNOWN
        , EGPUBufferUsage::Default
        , EGPUBufferBindFlag_ConstantBuffer ) );
    if ( !m_ControlConstantBuffer )
        return false;

    m_NewPathConstantBuffer.reset( GPUBuffer::Create(
          sizeof( SNewPathConstants )
        , 0
        , DXGI_FORMAT_UNKNOWN
        , EGPUBufferUsage::Default
        , EGPUBufferBindFlag_ConstantBuffer ) );
    if ( !m_NewPathConstantBuffer )
        return false;

    m_MaterialConstantBuffer.reset( GPUBuffer::Create(
          sizeof( SMaterialConstants )
        , 0
        , DXGI_FORMAT_UNKNOWN
        , EGPUBufferUsage::Default
        , EGPUBufferBindFlag_ConstantBuffer ) );
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
    m_LightSamplingResultsBuffer.reset();
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
        m_PSOs[ i ].reset();
    }

    m_RootSignature.Reset();
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

void CWavefrontPathTracer::Render( const SRenderContext& renderContext, const SBxDFTextures& BxDFTextures )
{
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
            constantBufferUploadContexts[ 0 ].Unmap();
        }
    }

    // Fill new path constants buffer
    if ( m_NewPathConstantBuffer->AllocateUploadContext( &constantBufferUploadContexts[ 1 ] ) )
    { 
        SNewPathConstants* constants = (SNewPathConstants*)constantBufferUploadContexts[ 1 ].Map();
        if ( constants )
        {
            m_Scene->m_Camera.GetTransformMatrix( &constants->g_CameraTransform );
            constants->g_Resolution[ 0 ] = renderContext.m_CurrentResolutionWidth;
            constants->g_Resolution[ 1 ] = renderContext.m_CurrentResolutionHeight;
            constants->g_FilmSize = m_Scene->m_FilmSize;
            constants->g_ApertureRadius = m_Scene->CalculateApertureDiameter() * 0.5f;
            constants->g_FocalDistance = m_Scene->m_FocalDistance;
            constants->g_FilmDistance = m_Scene->CalculateFilmDistance();
            constants->g_BladeCount = m_Scene->m_ApertureBladeCount;
            float halfBladeAngle = DirectX::XM_PI / m_Scene->m_ApertureBladeCount;
            constants->g_BladeVertexPos.x = cosf( halfBladeAngle ) * constants->g_ApertureRadius;
            constants->g_BladeVertexPos.y = sinf( halfBladeAngle ) * constants->g_ApertureRadius;
            constants->g_ApertureBaseAngle = m_Scene->m_ApertureRotation;
            constantBufferUploadContexts[ 1 ].Unmap();
        }
    }

    // Fill material constants buffer
    if ( m_MaterialConstantBuffer->AllocateUploadContext( &constantBufferUploadContexts[ 2 ] ) )
    { 
        SMaterialConstants* constants = (SMaterialConstants*)constantBufferUploadContexts[ 2 ].Map();
        if ( constants )
        {
            constants->g_LightCount = m_Scene->GetLightCount();
            constants->g_MaxBounceCount = m_Scene->m_MaxBounceCount;
            constants->g_EnvironmentLightIndex = m_Scene->m_EnvironmentLight ? (uint32_t)m_Scene->m_MeshLights.size() : LIGHT_INDEX_INVALID; // Environment light is right after the mesh lights.
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
            D3D12Adapter::GetDevice()->CopyDescriptorsSimple( 1, nextBlockIndexDesciptor.CPU, m_NextBlockIndexBuffer->GetUAV().CPU, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

            // Clear the nextblockindex
            uint32_t value[ 4 ] = { 0 };
            commandList->ClearUnorderedAccessViewUint( nextBlockIndexDesciptor.GPU, m_NextBlockIndexBuffer->GetUAV().CPU, m_NextBlockIndexBuffer->GetBuffer(), value, 0, nullptr );
        }

        {
            D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition( m_ControlConstantBuffer->GetBuffer(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER );
            commandList->ResourceBarrier( 1, &barrier );
        }

        {
            D3D12_GPU_DESCRIPTOR_HANDLE descriptorTable = s_DescriptorTableLayout.AllocateAndCopyToGPUDescriptorHeap( nullptr, 0, &m_FlagsBuffer->GetUAV(), 1 );
            commandList->SetComputeRootDescriptorTable( 3, descriptorTable );

            commandList->SetComputeRootConstantBufferView( 0, m_ControlConstantBuffer->GetGPUVirtualAddress() );
            commandList->SetPipelineState( m_PSOs[ (int)EShaderKernel::SetIdle ].get() );
            // Reset all path to idle
            commandList->Dispatch( CalculateDispatchGroupCount( s_PathPoolLaneCount ), 1, 1 );
        }
    }

    for ( uint32_t i = 0; i < m_IterationPerFrame; ++i )
    {
        RenderOneIteration( renderContext, BxDFTextures, i == 0 );
    }

    // Copy ray counter to the staging buffer
    {
        SCOPED_RENDER_ANNOTATION( commandList, L"Copy counters to staging buffer" );

        if ( m_NewImage )
        {
            // For new image, initialize all staging buffer
            for ( uint32_t i = 0; i < s_QueueCounterStagingBufferCount; ++i )
            {
                commandList->CopyResource( m_QueueCounterStagingBuffer[ i ]->GetBuffer(), m_QueueCounterBuffers[ 1 ]->GetBuffer() );
                m_QueueCounterStagingBufferIndex = 0;
            }
        }
        else
        {
            commandList->CopyResource( m_QueueCounterStagingBuffer[ m_QueueCounterStagingBufferIndex ]->GetBuffer(), m_QueueCounterBuffers[ 1 ]->GetBuffer() );
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
        uint32_t* counters = (uint32_t*)address;
        bool areAllQueuesEmpty = !counters[ 0 ] && !counters[ 1 ];
        m_QueueCounterStagingBuffer[ m_QueueCounterStagingBufferIndex ]->Unmap();
        return areAllQueuesEmpty;
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
    rayTracingShaderDefines.push_back( { NULL, NULL } );

    const char* s_KernelDefines[] = { "EXTENSION_RAY_CAST", "SHADOW_RAY_CAST", "NEW_PATH", "MATERIAL", "CONTROL", "FILL_INDIRECT_ARGUMENTS", "SET_IDLE" };
    rayTracingShaderDefines.insert( rayTracingShaderDefines.begin(), { s_KernelDefines[ (int)kernel ], "0" } );

    ComputeShaderPtr shader( ComputeShader::CreateFromFile( L"Shaders\\WavefrontPathTracing.hlsl", rayTracingShaderDefines ) );
    if ( !shader )
    {
        CMessagebox::GetSingleton().AppendFormat( "Failed to compile ray tracing shader kernel \"%s\".\n", s_KernelDefines[ (int)kernel ] );
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

    m_PSOs[ (uint32_t)kernel ].reset( PSO, SD3D12ComDeferredDeleter() );

    return true;
}

void CWavefrontPathTracer::GetBlockDimension( uint32_t* width, uint32_t* height )
{
    uint32_t blockSizeWidth[ s_BlockDimensionCount ] = { 8, 4 };
    uint32_t blockSizeHeight[ s_BlockDimensionCount ] = { 4, 8 };
    *width = blockSizeWidth[ m_BlockDimensionIndex ];
    *height = blockSizeHeight[ m_BlockDimensionIndex ];
}

void CWavefrontPathTracer::RenderOneIteration( const SRenderContext& renderContext, const SBxDFTextures& BxDFTextures, bool isInitialIteration )
{
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

        GPUBufferPtr queueCounterBuffer = m_QueueCounterBuffers[ 1 ];
        SD3D12GPUDescriptorHeapHandle queueCounterDescriptor = GPUDescriptorHeap->Allocate( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
        D3D12Adapter::GetDevice()->CopyDescriptorsSimple( 1, queueCounterDescriptor.CPU, queueCounterBuffer->GetUAV().CPU, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

        uint32_t value[ 4 ] = { 0 };
        commandList->ClearUnorderedAccessViewUint( queueCounterDescriptor.GPU, queueCounterBuffer->GetUAV().CPU, queueCounterBuffer->GetBuffer(), value, 0, nullptr );
    }

    // Barriers
    {
        std::vector<D3D12_RESOURCE_BARRIER> barriers;
        barriers.reserve( 11 );

        if ( m_Scene->m_IsSampleTexturesRead )
        { 
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_Scene->m_SamplePositionTexture->GetTexture(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS ) );
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_Scene->m_SampleValueTexture->GetTexture(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS ) );
            m_Scene->m_IsSampleTexturesRead = false;
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
        
        commandList->ResourceBarrier( barriers.size(), barriers.data() );
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
            , m_Scene->m_SamplePositionTexture->GetUAV()
            , m_Scene->m_SampleValueTexture->GetUAV()
        };
        D3D12_GPU_DESCRIPTOR_HANDLE descriptorTable = s_DescriptorTableLayout.AllocateAndCopyToGPUDescriptorHeap( SRVs, ARRAY_LENGTH( SRVs ), UAVs, ARRAY_LENGTH( UAVs ) );

        commandList->SetComputeRootConstantBufferView( 0, m_ControlConstantBuffer->GetGPUVirtualAddress() );
        commandList->SetComputeRootDescriptorTable( 3, descriptorTable );
        commandList->SetPipelineState( m_PSOs[ (int)EShaderKernel::Control ].get() );
        commandList->Dispatch( CalculateDispatchGroupCount( s_PathPoolLaneCount ), 1, 1 );
    }

    // Barriers
    if ( !isInitialIteration )
    {
        D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition( m_QueueCounterBuffers[ 0 ]->GetBuffer(),
            D3D12_RESOURCE_STATE_COPY_SOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
        commandList->ResourceBarrier( 1, &barrier );
    }

    // Clear ray counters
    {
        SCOPED_RENDER_ANNOTATION( commandList, L"Clear extension & shadow raycast queues" );

        GPUBufferPtr queueCounterBuffer = m_QueueCounterBuffers[ 0 ];
        SD3D12GPUDescriptorHeapHandle queueCounterDescriptor = GPUDescriptorHeap->Allocate( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
        D3D12Adapter::GetDevice()->CopyDescriptorsSimple( 1, queueCounterDescriptor.CPU, queueCounterBuffer->GetUAV().CPU, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

        uint32_t value[ 4 ] = { 0 };
        commandList->ClearUnorderedAccessViewUint( queueCounterDescriptor.GPU, queueCounterBuffer->GetUAV().CPU, queueCounterBuffer->GetBuffer(), value, 0, nullptr );
    }

    // Barriers
    {
        std::vector<D3D12_RESOURCE_BARRIER> barriers;
        barriers.reserve( 2 );

        barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_QueueCounterBuffers[ 1 ]->GetBuffer(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE ) );

        if ( !isInitialIteration )
        {
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_QueueConstantsBuffers[ 1 ]->GetBuffer(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, D3D12_RESOURCE_STATE_COPY_DEST ) );
        }

        commandList->ResourceBarrier( barriers.size(), barriers.data() );
    }

    // Copy the new path & material queue counters to constant buffers
    {
        SCOPED_RENDER_ANNOTATION( commandList, L"Copy new path & material queue counter" );

        commandList->CopyResource( m_QueueConstantsBuffers[ 1 ]->GetBuffer(), m_QueueCounterBuffers[ 1 ]->GetBuffer() );
    }

    // Barriers
    if ( !isInitialIteration )
    {
        D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition( m_IndirectArgumentBuffer[ (int)EShaderKernel::NewPath ]->GetBuffer(),
            D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
        commandList->ResourceBarrier( 1, &barrier );
    }

    // Fill indirect args for new path
    {
        SCOPED_RENDER_ANNOTATION( commandList, L"Fill new path indirect arg" );

        SD3D12DescriptorHandle SRV = m_QueueCounterBuffers[ 1 ]->GetSRV( DXGI_FORMAT_R32_UINT, 4, 1, 1 );
        SD3D12DescriptorHandle UAV = m_IndirectArgumentBuffer[ (int)EShaderKernel::NewPath ]->GetUAV();
        D3D12_GPU_DESCRIPTOR_HANDLE descriptorTable = s_DescriptorTableLayout.AllocateAndCopyToGPUDescriptorHeap( &SRV, 1, &UAV, 1 );

        commandList->SetComputeRootDescriptorTable( 3, descriptorTable );
        commandList->SetPipelineState( m_PSOs[ (int)EShaderKernel::FillIndirectArguments ].get() );
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

        SD3D12DescriptorHandle SRV = m_QueueCounterBuffers[ 1 ]->GetSRV( DXGI_FORMAT_R32_UINT, 4, 0, 1 );
        SD3D12DescriptorHandle UAV = m_IndirectArgumentBuffer[ (int)EShaderKernel::Material ]->GetUAV();
        D3D12_GPU_DESCRIPTOR_HANDLE descriptorTable = s_DescriptorTableLayout.AllocateAndCopyToGPUDescriptorHeap( &SRV, 1, &UAV, 1 );

        commandList->SetComputeRootDescriptorTable( 3, descriptorTable );
        commandList->SetPipelineState( m_PSOs[ (int)EShaderKernel::FillIndirectArguments ].get() );
        commandList->Dispatch( 1, 1, 1 );
    }

    // Barriers
    {
        std::vector<D3D12_RESOURCE_BARRIER> barriers;
        barriers.reserve( 10 );

        barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_QueueConstantsBuffers[ 1 ]->GetBuffer(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER ) );
        barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_QueueBuffers[ (int)EShaderKernel::NewPath ]->GetBuffer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE ) );
        barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_PixelPositionBuffer->GetBuffer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE ) );
        barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_PixelSampleBuffer->GetBuffer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS ) );
        barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_LightSamplingResultsBuffer->GetBuffer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS ) );
        barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::UAV( m_QueueCounterBuffers[ 0 ]->GetBuffer() ) );
        barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_IndirectArgumentBuffer[ (int)EShaderKernel::NewPath ]->GetBuffer(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT ) );

        if ( isInitialIteration )
        {
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( renderContext.m_RayTracingFrameConstantBuffer->GetBuffer(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER ) );
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_NewPathConstantBuffer->GetBuffer(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER ) );
        }
        else
        {
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_RayBuffer->GetBuffer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS ) );
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::UAV( m_RngBuffer->GetBuffer() ) );
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_QueueBuffers[ (int)EShaderKernel::ExtensionRayCast ]->GetBuffer(),
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS ) );
        }

        commandList->ResourceBarrier( barriers.size(), barriers.data() );
    }

    // New Path
    {
        SCOPED_RENDER_ANNOTATION( commandList, L"New path" );

        SD3D12DescriptorHandle SRVs[] =
        {
              m_QueueBuffers[ (int)EShaderKernel::NewPath ]->GetSRV()
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
        D3D12_GPU_DESCRIPTOR_HANDLE descriptorTable = s_DescriptorTableLayout.AllocateAndCopyToGPUDescriptorHeap( SRVs, ARRAY_LENGTH( SRVs ), UAVs, ARRAY_LENGTH( UAVs ) );

        commandList->SetComputeRootConstantBufferView( 0, m_QueueConstantsBuffers[ 1 ]->GetGPUVirtualAddress() );
        commandList->SetComputeRootConstantBufferView( 1, renderContext.m_RayTracingFrameConstantBuffer->GetGPUVirtualAddress() );
        commandList->SetComputeRootConstantBufferView( 2, m_NewPathConstantBuffer->GetGPUVirtualAddress() );
        commandList->SetComputeRootDescriptorTable( 3, descriptorTable );
        commandList->SetPipelineState( m_PSOs[ (int)EShaderKernel::NewPath ].get() );
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

        if ( !m_Scene->m_IsLightBufferRead )
        {
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_Scene->m_LightsBuffer->GetBuffer(),
                D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE ) );
            m_Scene->m_IsLightBufferRead = true;
        }
        if ( !m_Scene->m_IsMaterialBufferRead )
        {
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_Scene->m_MaterialsBuffer->GetBuffer(),
                D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE ) );
            m_Scene->m_IsMaterialBufferRead = true;
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

        commandList->ResourceBarrier( barriers.size(), barriers.data() );
    }

    // Material
    {
        SCOPED_RENDER_ANNOTATION( commandList, L"Material" );

        SD3D12DescriptorHandle environmentTextureSRV;
        if ( m_Scene->m_EnvironmentLight && m_Scene->m_EnvironmentLight->m_Texture )
        {
            environmentTextureSRV = m_Scene->m_EnvironmentLight->m_Texture->GetSRV();
        }

        SD3D12DescriptorHandle SRVs[] =
        {
              m_QueueBuffers[ (int)EShaderKernel::Material ]->GetSRV()
            , m_RayHitBuffer->GetSRV()
            , m_Scene->m_VerticesBuffer->GetSRV()
            , m_Scene->m_TrianglesBuffer->GetSRV()
            , m_Scene->m_LightsBuffer->GetSRV()
            , m_Scene->m_InstanceTransformsBuffer->GetSRV( DXGI_FORMAT_UNKNOWN, sizeof( XMFLOAT4X3 ), 0, (uint32_t)m_Scene->m_InstanceTransforms.size() )
            , m_Scene->m_MaterialIdsBuffer->GetSRV()
            , m_Scene->m_MaterialsBuffer->GetSRV()
            , m_Scene->m_InstanceLightIndicesBuffer->GetSRV()
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
        D3D12_GPU_DESCRIPTOR_HANDLE descriptorTable = s_DescriptorTableLayout.AllocateAndCopyToGPUDescriptorHeap( SRVs, ARRAY_LENGTH( SRVs ), UAVs, ARRAY_LENGTH( UAVs ) );

        commandList->SetComputeRootConstantBufferView( 0, m_QueueConstantsBuffers[ 1 ]->GetGPUVirtualAddress() );
        commandList->SetComputeRootConstantBufferView( 1, m_MaterialConstantBuffer->GetGPUVirtualAddress() );
        commandList->SetComputeRootDescriptorTable( 3, descriptorTable );
        commandList->SetPipelineState( m_PSOs[ (int)EShaderKernel::Material ].get() );
        commandList->ExecuteIndirect( D3D12Adapter::GetDispatchIndirectCommandSignature(), 1, m_IndirectArgumentBuffer[ (int)EShaderKernel::Material ]->GetBuffer(), 0, nullptr, 0 );
    }

    // Barrier
    {
        std::vector<D3D12_RESOURCE_BARRIER> barriers;
        barriers.reserve( 2 );

        barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_QueueCounterBuffers[ 0 ],
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE ) );

        if ( !isInitialIteration )
        {
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_QueueConstantsBuffers[ 0 ]->GetBuffer(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, D3D12_RESOURCE_STATE_COPY_DEST ) );
        }

        commandList->ResourceBarrier( barriers.size(), barriers.data() );
    }

    // Copy the extension ray & shadow ray queue counters to constant buffers
    {
        SCOPED_RENDER_ANNOTATION( commandList, L"Copy extension & shadow raycast queue counters" );

        commandList->CopyResource( m_QueueConstantsBuffers[ 0 ]->GetBuffer(), m_QueueCounterBuffers[ 0 ]->GetBuffer() );
    }

    // Barriers
    if ( !isInitialIteration )
    {
        D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition( m_IndirectArgumentBuffer[ (int)EShaderKernel::ExtensionRayCast ]->GetBuffer(),
            D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
        commandList->ResourceBarrier( 1, &barrier );
    }

    // Fill indirect args for extension raycast
    {
        SCOPED_RENDER_ANNOTATION( commandList, L"Fill extension raycast indirect arg" );

        SD3D12DescriptorHandle SRV = m_QueueCounterBuffers[ 0 ]->GetSRV( DXGI_FORMAT_R32_UINT, 4, 0, 1 );
        SD3D12DescriptorHandle UAV = m_IndirectArgumentBuffer[ (int)EShaderKernel::ExtensionRayCast ]->GetUAV();
        D3D12_GPU_DESCRIPTOR_HANDLE descriptorTable = s_DescriptorTableLayout.AllocateAndCopyToGPUDescriptorHeap( &SRV, 1, &UAV, 1 );

        commandList->SetComputeRootDescriptorTable( 3, descriptorTable );
        commandList->SetPipelineState( m_PSOs[ (int)EShaderKernel::FillIndirectArguments ].get() );
        commandList->Dispatch( 1, 1, 1 );
    }

    // Barriers
    if ( !isInitialIteration )
    {
        D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition( m_IndirectArgumentBuffer[ (int)EShaderKernel::ShadowRayCast ]->GetBuffer(),
            D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
        commandList->ResourceBarrier( 1, &barrier );
    }

    // Fill indirect args for shadow raycast
    {
        SCOPED_RENDER_ANNOTATION( commandList, L"Fill shadow raycast indirect arg" );

        SD3D12DescriptorHandle SRV = m_QueueCounterBuffers[ 0 ]->GetSRV( DXGI_FORMAT_R32_UINT, 4, 1, 1 );
        SD3D12DescriptorHandle UAV = m_IndirectArgumentBuffer[ (int)EShaderKernel::ShadowRayCast ]->GetUAV();
        D3D12_GPU_DESCRIPTOR_HANDLE descriptorTable = s_DescriptorTableLayout.AllocateAndCopyToGPUDescriptorHeap( &SRV, 1, &UAV, 1 );

        commandList->SetComputeRootDescriptorTable( 3, descriptorTable );
        commandList->SetPipelineState( m_PSOs[ (int)EShaderKernel::FillIndirectArguments ].get() );
        commandList->Dispatch( 1, 1, 1 );
    }

    // Barriers
    {
        std::vector<D3D12_RESOURCE_BARRIER> barriers;
        barriers.reserve( 5 );

        barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_QueueConstantsBuffers[ 0 ]->GetBuffer(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER ) );
        barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_RayBuffer->GetBuffer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE ) );
        barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_QueueBuffers[ (int)EShaderKernel::ExtensionRayCast ]->GetBuffer(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE ) );
        barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_RayHitBuffer->GetBuffer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS ) );
        barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_IndirectArgumentBuffer[ (int)EShaderKernel::ExtensionRayCast ]->GetBuffer(), 
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT ) );

        commandList->ResourceBarrier( barriers.size(), barriers.data() );
    }

    // Extension raycast
    {
        SCOPED_RENDER_ANNOTATION( commandList, L"Extension raycast" );

        SD3D12DescriptorHandle SRVs[] = 
        {
              m_Scene->m_VerticesBuffer->GetSRV()
            , m_Scene->m_TrianglesBuffer->GetSRV()
            , m_Scene->m_BVHNodesBuffer->GetSRV()
            , m_RayBuffer->GetSRV()
            , m_QueueBuffers[ (int)EShaderKernel::ExtensionRayCast ]->GetSRV()
            , m_Scene->m_InstanceTransformsBuffer->GetSRV( DXGI_FORMAT_UNKNOWN, sizeof( XMFLOAT4X3 ), (uint32_t)m_Scene->m_InstanceTransforms.size(), (uint32_t)m_Scene->m_InstanceTransforms.size() );
        };
        SD3D12DescriptorHandle UAVs[] = { m_RayHitBuffer->GetUAV() };
        D3D12_GPU_DESCRIPTOR_HANDLE descriptorTable = s_DescriptorTableLayout.AllocateAndCopyToGPUDescriptorHeap( SRVs, ARRAY_LENGTH( SRVs ), UAVs, ARRAY_LENGTH( UAVs ) );

        commandList->SetComputeRootDescriptorTable( 3, descriptorTable );
        commandList->SetComputeRootConstantBufferView( 0, m_QueueConstantsBuffers[ 0 ]->GetGPUVirtualAddress() );
        commandList->SetPipelineState( m_PSOs[ (int)EShaderKernel::ExtensionRayCast ].get() );
        commandList->ExecuteIndirect( D3D12Adapter::GetDispatchIndirectCommandSignature(), 1, m_IndirectArgumentBuffer[ (int)EShaderKernel::ExtensionRayCast ]->GetBuffer(), 0, nullptr, 0 );
    }

    // Barriers
    {
        std::vector<D3D12_RESOURCE_BARRIER> barriers;

        barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_ShadowRayBuffer->GetBuffer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE ) );
        barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_QueueBuffers[ (int)EShaderKernel::ShadowRayCast ]->GetBuffer(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE ) );
        barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::UAV( m_FlagsBuffer->GetBuffer() ) );
        barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_IndirectArgumentBuffer[ (int)EShaderKernel::ShadowRayCast ]->GetBuffer(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT ) );

        commandList->ResourceBarrier( barriers.size(), barriers.data() );
    }

    // Shadow raycast
    {
        SCOPED_RENDER_ANNOTATION( commandList, L"Shadow raycast" );

        SD3D12DescriptorHandle SRVs[] = 
        {
              m_Scene->m_VerticesBuffer->GetSRV()
            , m_Scene->m_TrianglesBuffer->GetSRV()
            , m_Scene->m_BVHNodesBuffer->GetSRV()
            , m_ShadowRayBuffer->GetSRV()
            , m_QueueBuffers[ (int)EShaderKernel::ShadowRayCast ]->GetSRV()
            , m_Scene->m_InstanceTransformsBuffer->GetSRV( DXGI_FORMAT_UNKNOWN, sizeof( XMFLOAT4X3 ), (uint32_t)m_Scene->m_InstanceTransforms.size(), (uint32_t)m_Scene->m_InstanceTransforms.size() );
        };
        SD3D12DescriptorHandle UAVs[] = { m_FlagsBuffer->GetUAV() };
        D3D12_GPU_DESCRIPTOR_HANDLE descriptorTable = s_DescriptorTableLayout.AllocateAndCopyToGPUDescriptorHeap( SRVs, ARRAY_LENGTH( SRVs ), UAVs, ARRAY_LENGTH( UAVs ) );

        commandList->SetComputeRootDescriptorTable( 3, descriptorTable );
        commandList->SetComputeRootConstantBufferView( 0, m_QueueConstantsBuffers[ 0 ]->GetGPUVirtualAddress() );
        commandList->SetPipelineState( m_PSOs[ (int)EShaderKernel::ShadowRayCast ].get() );
        commandList->ExecuteIndirect( D3D12Adapter::GetDispatchIndirectCommandSignature(), 1, m_IndirectArgumentBuffer[ (int)EShaderKernel::ShadowRayCast ]->GetBuffer(), 0, nullptr, 0 );
    }
}
