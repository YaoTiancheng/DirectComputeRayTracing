#include "stdafx.h"
#include "SceneLuminanceRenderer.h"
#include "Shader.h"
#include "GPUBuffer.h"
#include "GPUTexture.h"
#include "Scene.h"
#include "ScopedRenderAnnotation.h"
#include "imgui/imgui.h"
#include "../Shaders/SumLuminanceDef.inc.hlsl"

bool SceneLuminanceRenderer::Init()
{
    ComputeShaderPtr sumLuminanceToSingleShader, sumLuminanceTo1DShader;
    {
        std::vector<D3D_SHADER_MACRO> sumLuminanceShaderDefines;
        sumLuminanceShaderDefines.push_back( { NULL, NULL } );
        sumLuminanceToSingleShader.reset( ComputeShader::CreateFromFile( L"Shaders\\SumLuminance.hlsl", sumLuminanceShaderDefines ) );
        if ( !sumLuminanceToSingleShader )
            return false;

        sumLuminanceShaderDefines.insert( sumLuminanceShaderDefines.begin(), { "REDUCE_TO_1D", "0" } );
        sumLuminanceTo1DShader.reset( ComputeShader::CreateFromFile( L"Shaders\\SumLuminance.hlsl", sumLuminanceShaderDefines ) );
        if ( !sumLuminanceTo1DShader )
            return false;
    }

    // Create root signature
    {
        CD3DX12_ROOT_PARAMETER1 rootParameters[ 3 ];
        rootParameters[ 0 ].InitAsConstantBufferView( 0 );
        rootParameters[ 1 ].InitAsShaderResourceView( 0 );
        rootParameters[ 2 ].InitAsUnorderedAccessView( 0 );
        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc( 3, rootParameters );

        ComPtr<ID3DBlob> serializedRootSignature;
        ComPtr<ID3DBlob> error;
        HRESULT hr = D3D12SerializeVersionedRootSignature( &rootSignatureDesc, serializedRootSignature.GetAddressOf(), error.GetAddressOf() ); 
        LOG_STRING_FORMAT( "Create scene luminance root signature with error: %s\n", (const char*)error->GetBufferPointer() );
        if ( FAILED( hr ) )
        {
            return false;
        }

        if ( FAILED( D3D12Adapter::GetDevice()->CreateRootSignature( 0, serializedRootSignature->GetBufferPointer(), serializedRootSignature->GetBufferSize(), IID_PPV_ARGS( m_RootSignature.GetAddressOf() ) ) ) )
        {
            return false;
        }
    }

    // Create PSOs
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = m_RootSignature.Get();
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

bool SceneLuminanceRenderer::SetFilmTexture( uint32_t resolutionWidth, uint32_t resolutionHeight, const GPUTexturePtr& filmTexture )
{
    uint32_t sumLuminanceBlockCountX = uint32_t( std::ceilf( resolutionWidth / float( SL_BLOCKSIZE ) ) );
    sumLuminanceBlockCountX = uint32_t( std::ceilf( sumLuminanceBlockCountX / 2.0f ) );
    uint32_t sumLuminanceBlockCountY = uint32_t( std::ceilf( resolutionHeight / float( SL_BLOCKSIZEY ) ) );
    sumLuminanceBlockCountY = uint32_t( std::ceilf( sumLuminanceBlockCountY / 2.0f ) );
    m_SumLuminanceBuffer0.reset( GPUBuffer::CreateStructured(
          sizeof( float ) * sumLuminanceBlockCountX * sumLuminanceBlockCountY
        , sizeof( float )
        , EGPUBufferUsage::Default
        , EGPUBufferBindFlag_UnorderedAccess | EGPUBufferBindFlag_ShaderResource ), SD3D12ResourceDeferredDeleter() );
    if ( !m_SumLuminanceBuffer0 )
        return false;
    m_SumLuminanceBuffer1.reset( GPUBuffer::CreateStructured(
          sizeof( float ) * sumLuminanceBlockCountX * sumLuminanceBlockCountY
        , sizeof( float )
        , EGPUBufferUsage::Default
        , EGPUBufferBindFlag_UnorderedAccess | EGPUBufferBindFlag_ShaderResource ), SD3D12ResourceDeferredDeleter() );
    if ( !m_SumLuminanceBuffer1 )
        return false;

    m_LuminanceResultBuffer = nullptr;

    return true;
}

void SceneLuminanceRenderer::Dispatch( const CScene& scene, uint32_t resolutionWidth, uint32_t resolutionHeight )
{
    SCOPED_RENDER_ANNOTATION( L"Calculate scene luminance" );

    ID3D12GraphicsCommandList* commandList = D3D12Adapter::GetCommandList();

    uint32_t sumLuminanceBlockCountX = uint32_t( std::ceilf( resolutionWidth / float( SL_BLOCKSIZE ) ) );
    sumLuminanceBlockCountX = uint32_t( std::ceilf( sumLuminanceBlockCountX / 2.0f ) );
    uint32_t sumLuminanceBlockCountY = uint32_t( std::ceilf( resolutionHeight / float( SL_BLOCKSIZEY ) ) );
    sumLuminanceBlockCountY = uint32_t( std::ceilf( sumLuminanceBlockCountY / 2.0f ) );

    commandList->SetComputeRootSignature( m_RootSignature.Get() );

    // Create constant buffer
    {   
        uint32_t params[ 4 ];
        params[ 0 ] = sumLuminanceBlockCountX;
        params[ 1 ] = sumLuminanceBlockCountY;
        params[ 2 ] = resolutionWidth;
        params[ 3 ] = resolutionHeight;
        GPUBufferPtr constantBuffer( GPUBuffer::Create( sizeof( uint32_t ) * 4, 0, DXGI_FORMAT_UNKNOWN, EGPUBufferUsage::Dynamic, EGPUBufferBindFlag_ConstantBuffer, params ),
            SD3D12ResourceDeferredDeleter() );

        commandList->SetComputeRootConstantBufferView( 0, constantBuffer->GetGPUVirtualAddress() );
    }

    // Resource barrier
    {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = m_SumLuminanceBuffer1->GetBuffer();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        commandList->ResourceBarrier( 1, barrier );
    }

    commandList->SetComputeRootUnorderedAccessView( 2, m_SumLuminanceBuffer1->GetUAV().GPU.ptr );
    commandList->SetComputeRootShaderResourceView( 1, scene.m_FilmTexture->GetSRV().GPU.ptr );
    commandList->SetPipelineState( m_SumLuminanceTo1DPSO.Get() );
    commandList->Dispatch( sumLuminanceBlockCountX, sumLuminanceBlockCountY, 1 );

    // Switch the PSO
    commandList->SetPipelineState( m_SumLuminanceToSinglePSO.Get() );

    GPUBuffer* sumLuminanceBuffer0 = m_SumLuminanceBuffer0.get();
    GPUBuffer* sumLuminanceBuffer1 = m_SumLuminanceBuffer1.get();
    uint32_t blockCount = sumLuminanceBlockCountX * sumLuminanceBlockCountY;
    while ( blockCount != 1 )
    {
        // Transition the ping-pong buffers
        {
            D3D12_RESOURCE_BARRIER barriers[ 2 ] = {};
            barriers[ 0 ].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[ 0 ].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barriers[ 0 ].Transition.pResource = sumLuminanceBuffer0->GetBuffer();
            barriers[ 0 ].Transition.StateBefore = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
            barriers[ 0 ].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            barriers[ 1 ].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[ 1 ].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barriers[ 1 ].Transition.pResource = sumLuminanceBuffer1->GetBuffer();
            barriers[ 1 ].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            barriers[ 1 ].Transition.StateAfter = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
            commandList->ResourceBarrier( 2, barriers );
        }

        commandList->SetComputeRootUnorderedAccessView( 2, sumLuminanceBuffer0->GetUAV().GPU.ptr );
        commandList->SetComputeRootShaderResourceView( 1, sumLuminanceBuffer1->GetSRV().GPU.ptr );

        uint32_t threadGroupCount = uint32_t( std::ceilf( blockCount / float( SL_REDUCE_TO_SINGLE_GROUPTHREADS ) ) );

        // Allocate a constant buffer
        {
            uint32_t params[ 4 ];
            params[ 0 ] = blockCount;
            params[ 1 ] = threadGroupCount;
            GPUBufferPtr constantBuffer( GPUBuffer::Create( sizeof( uint32_t ) * 4, 0, DXGI_FORMAT_UNKNOWN, EGPUBufferUsage::Dynamic, EGPUBufferBindFlag_ConstantBuffer, params ),
                SD3D12ResourceDeferredDeleter() );

            commandList->SetComputeRootConstantBufferView( 0, constantBuffer->GetGPUVirtualAddress() );
        }

        commandList->Dispatch( threadGroupCount, 1, 1 );

        blockCount = threadGroupCount;

        std::swap( sumLuminanceBuffer0, sumLuminanceBuffer1 );
    }

    // Transition the result buffer for shader read. But this should be delayed to where it is actually used.
    {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = sumLuminanceBuffer1->GetBuffer();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
        commandList->ResourceBarrier( 1, barrier );
    }

    m_LuminanceResultBuffer = sumLuminanceBuffer1;
}

void SceneLuminanceRenderer::OnImGUI()
{
}
