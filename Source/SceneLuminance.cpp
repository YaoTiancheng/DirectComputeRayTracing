#include "stdafx.h"
#include "DirectComputeRayTracing.h"
#include "D3D12Adapter.h"
#include "D3D12DescriptorUtil.h"
#include "Shader.h"
#include "GPUBuffer.h"
#include "GPUTexture.h"
#include "ScopedRenderAnnotation.h"
#include "Logging.h"
#include "RenderContext.h"
#include "../Shaders/SumLuminanceDef.inc.hlsl"

using namespace D3D12Util;
using SRenderer = CDirectComputeRayTracing::SRenderer;

static SD3D12DescriptorTableLayout s_DescriptorTableLayout = SD3D12DescriptorTableLayout( 1, 1 );

bool SRenderer::InitSceneLuminance()
{
    ComputeShaderPtr sumLuminanceToSingleShader, sumLuminanceTo1DShader;
    {
        std::vector<DxcDefine> sumLuminanceShaderDefines;
        sumLuminanceToSingleShader.reset( ComputeShader::CreateFromFile( L"Shaders\\SumLuminance.hlsl", sumLuminanceShaderDefines ) );
        if ( !sumLuminanceToSingleShader )
            return false;

        sumLuminanceShaderDefines.push_back( { L"REDUCE_TO_1D", L"0" } );
        sumLuminanceTo1DShader.reset( ComputeShader::CreateFromFile( L"Shaders\\SumLuminance.hlsl", sumLuminanceShaderDefines ) );
        if ( !sumLuminanceTo1DShader )
            return false;
    }

    // Create root signature
    {
        CD3DX12_ROOT_PARAMETER1 rootParameters[ 2 ];
        rootParameters[ 0 ].InitAsConstants( 4, 0 );
        SD3D12DescriptorTableRanges descriptorTableRanges;
        s_DescriptorTableLayout.InitRootParameter( &rootParameters[ 1 ], &descriptorTableRanges );
        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc( 2, rootParameters );

        ComPtr<ID3DBlob> serializedRootSignature;
        ComPtr<ID3DBlob> error;
        HRESULT hr = D3D12SerializeVersionedRootSignature( &rootSignatureDesc, serializedRootSignature.GetAddressOf(), error.GetAddressOf() ); 
        if ( error )
        {
            LOG_STRING_FORMAT( "Create scene luminance root signature with error: %s\n", (const char*)error->GetBufferPointer() );
        }
        if ( FAILED( hr ) )
        {
            return false;
        }

        if ( FAILED( D3D12Adapter::GetDevice()->CreateRootSignature( 0, serializedRootSignature->GetBufferPointer(), serializedRootSignature->GetBufferSize(),
            IID_PPV_ARGS( m_SceneLuminanceRootSignature.GetAddressOf() ) ) ) )
        {
            return false;
        }
    }

    // Create PSOs
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = m_SceneLuminanceRootSignature.Get();
        desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

        // SumLuminanceTo1D PSO
        desc.CS = sumLuminanceTo1DShader->GetShaderBytecode();
        if ( FAILED( D3D12Adapter::GetDevice()->CreateComputePipelineState( &desc, IID_PPV_ARGS( m_SumLuminanceTo1DPSO.GetAddressOf() ) ) ) )
        {
            return false;
        }

        // SumLuminanceToSingle PSO
        desc.CS = sumLuminanceToSingleShader->GetShaderBytecode();
        if ( FAILED( D3D12Adapter::GetDevice()->CreateComputePipelineState( &desc, IID_PPV_ARGS( m_SumLuminanceToSinglePSO.GetAddressOf() ) ) ) )
        {
            return false;
        }
    }

    return true;
}

bool SRenderer::ResizeSceneLuminanceInputResolution( uint32_t resolutionWidth, uint32_t resolutionHeight )
{
    uint32_t sumLuminanceBlockCountX = uint32_t( std::ceilf( resolutionWidth / float( SL_BLOCKSIZE ) ) );
    sumLuminanceBlockCountX = uint32_t( std::ceilf( sumLuminanceBlockCountX / 2.0f ) );
    uint32_t sumLuminanceBlockCountY = uint32_t( std::ceilf( resolutionHeight / float( SL_BLOCKSIZEY ) ) );
    sumLuminanceBlockCountY = uint32_t( std::ceilf( sumLuminanceBlockCountY / 2.0f ) );
    m_SumLuminanceBuffer0.Reset( GPUBuffer::CreateStructured(
          sizeof( float ) * sumLuminanceBlockCountX * sumLuminanceBlockCountY
        , sizeof( float )
        , EGPUBufferUsage::Default
        , EGPUBufferBindFlag_UnorderedAccess | EGPUBufferBindFlag_ShaderResource ) );
    if ( !m_SumLuminanceBuffer0 )
        return false;
    m_SumLuminanceBuffer1.Reset( GPUBuffer::CreateStructured(
          sizeof( float ) * sumLuminanceBlockCountX * sumLuminanceBlockCountY
        , sizeof( float )
        , EGPUBufferUsage::Default
        , EGPUBufferBindFlag_UnorderedAccess | EGPUBufferBindFlag_ShaderResource ) );
    if ( !m_SumLuminanceBuffer1 )
        return false;

    m_LuminanceResultBuffer = nullptr;

    return true;
}

void SRenderer::DispatchSceneLuminanceCompute( const SRenderContext& renderContext )
{
    if ( !m_IsAutoExposureEnabled || !m_IsPostFXEnabled || !renderContext.m_EnablePostFX )
    {
        return;
    }

    const uint32_t resolutionWidth = renderContext.m_CurrentResolutionWidth;
    const uint32_t resolutionHeight = renderContext.m_CurrentResolutionHeight;

    ID3D12GraphicsCommandList* commandList = D3D12Adapter::GetCommandList();

    SCOPED_RENDER_ANNOTATION( commandList, L"Calculate scene luminance" );

    uint32_t sumLuminanceBlockCountX = uint32_t( std::ceilf( resolutionWidth / float( SL_BLOCKSIZE ) ) );
    sumLuminanceBlockCountX = uint32_t( std::ceilf( sumLuminanceBlockCountX / 2.0f ) );
    uint32_t sumLuminanceBlockCountY = uint32_t( std::ceilf( resolutionHeight / float( SL_BLOCKSIZEY ) ) );
    sumLuminanceBlockCountY = uint32_t( std::ceilf( sumLuminanceBlockCountY / 2.0f ) );

    commandList->SetComputeRootSignature( m_SceneLuminanceRootSignature.Get() );

    // Set constant buffer
    {   
        uint32_t params[ 4 ];
        params[ 0 ] = sumLuminanceBlockCountX;
        params[ 1 ] = sumLuminanceBlockCountY;
        params[ 2 ] = resolutionWidth;
        params[ 3 ] = resolutionHeight;
        commandList->SetComputeRoot32BitConstants( 0, 4, params, 0 );
    }

    // Barriers
    {
        D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition( m_Scene.m_FilmTexture->GetTexture(),
            m_Scene.m_FilmTextureStates, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE );
        m_Scene.m_FilmTextureStates = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
        commandList->ResourceBarrier( 1, &barrier );
    }

    D3D12_GPU_DESCRIPTOR_HANDLE descriptorTable = s_DescriptorTableLayout.AllocateAndCopyToGPUDescriptorHeap( &m_Scene.m_FilmTexture->GetSRV(), 1, &m_SumLuminanceBuffer1->GetUAV(), 1 );
    commandList->SetComputeRootDescriptorTable( 1, descriptorTable );

    commandList->SetPipelineState( m_SumLuminanceTo1DPSO.Get() );
    commandList->Dispatch( sumLuminanceBlockCountX, sumLuminanceBlockCountY, 1 );

    // Switch the PSO
    commandList->SetPipelineState( m_SumLuminanceToSinglePSO.Get() );

    GPUBuffer* sumLuminanceBuffer0 = m_SumLuminanceBuffer0.Get();
    GPUBuffer* sumLuminanceBuffer1 = m_SumLuminanceBuffer1.Get();
    uint32_t blockCount = sumLuminanceBlockCountX * sumLuminanceBlockCountY;
    uint32_t iteration = 0;
    while ( blockCount != 1 )
    {
        // Transition the ping-pong buffers
        {
            D3D12_RESOURCE_BARRIER barriers[ 2 ] = 
            {
                CD3DX12_RESOURCE_BARRIER::Transition( sumLuminanceBuffer1->GetBuffer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE ),
                CD3DX12_RESOURCE_BARRIER::Transition( sumLuminanceBuffer0->GetBuffer(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS ),
            };
            commandList->ResourceBarrier( iteration == 0 ? 1 : 2, barriers ); // The 1st iteration relies on state implicit promotion
        }

        descriptorTable = s_DescriptorTableLayout.AllocateAndCopyToGPUDescriptorHeap( &sumLuminanceBuffer1->GetSRV(), 1, &sumLuminanceBuffer0->GetUAV(), 1 );
        commandList->SetComputeRootDescriptorTable( 1, descriptorTable );

        uint32_t threadGroupCount = uint32_t( std::ceilf( blockCount / float( SL_REDUCE_TO_SINGLE_GROUPTHREADS ) ) );

        // Allocate a constant buffer
        {
            uint32_t params[ 4 ];
            params[ 0 ] = blockCount;
            params[ 1 ] = threadGroupCount;
            commandList->SetComputeRoot32BitConstants( 0, 4, params, 0 );
        }

        commandList->Dispatch( threadGroupCount, 1, 1 );

        blockCount = threadGroupCount;

        std::swap( sumLuminanceBuffer0, sumLuminanceBuffer1 );

        ++iteration;
    }

    m_LuminanceResultBuffer = sumLuminanceBuffer1;
}