#include "stdafx.h"
#include "SceneLuminanceRenderer.h"
#include "D3D12Adapter.h"
#include "D3D12GPUDescriptorHeap.h"
#include "Shader.h"
#include "GPUBuffer.h"
#include "GPUTexture.h"
#include "Scene.h"
#include "ScopedRenderAnnotation.h"
#include "Logging.h"
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
        CD3DX12_ROOT_PARAMETER1 rootParameters[ 2 ];
        rootParameters[ 0 ].InitAsConstantBufferView( 0 );
        CD3DX12_DESCRIPTOR_RANGE1 descriptorRanges[ 2 ];
        descriptorRanges[ 0 ].Init( D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0 );
        descriptorRanges[ 1 ].Init( D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0 );
        rootParameters[ 1 ].InitAsDescriptorTable( 2, descriptorRanges );
        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc( 2, rootParameters );

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

void SceneLuminanceRenderer::Dispatch( const CScene& scene, uint32_t resolutionWidth, uint32_t resolutionHeight )
{
    SCOPED_RENDER_ANNOTATION( L"Calculate scene luminance" );

    ID3D12GraphicsCommandList* commandList = D3D12Adapter::GetCommandList();
    CD3D12GPUDescriptorHeap* descriptorHeap = D3D12Adapter::GetGPUDescriptorHeap( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

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
        CD3D12ResourcePtr<GPUBuffer> constantBuffer( GPUBuffer::Create( sizeof( uint32_t ) * 4, 0, DXGI_FORMAT_UNKNOWN, 
            EGPUBufferUsage::Dynamic, EGPUBufferBindFlag_ConstantBuffer, params ) );

        commandList->SetComputeRootConstantBufferView( 0, constantBuffer->GetGPUVirtualAddress() );
    }

    CD3D12DescritorHandle descriptorTable = descriptorHeap->AllocateRange( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2 );
    commandList->SetComputeRootDescriptorTable( 1, descriptorTable.GPU );
    CD3D12DescritorHandle SRV = descriptorTable;
    CD3D12DescritorHandle UAV = descriptorTable.Offsetted( 1, D3D12Adapter::GetDescriptorSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ) );
    D3D12Adapter::GetDevice()->CopyDescriptorsSimple( 1, SRV.CPU, scene.m_FilmTexture->GetSRV().CPU, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
    D3D12Adapter::GetDevice()->CopyDescriptorsSimple( 1, UAV.CPU, m_SumLuminanceBuffer1->GetUAV().CPU, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

    commandList->SetPipelineState( m_SumLuminanceTo1DPSO.Get() );

    // Barriers
    {
        D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition( scene.m_FilmTexture->GetTexture(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE );
        commandList->ResourceBarrier( 1, &barrier );
    }

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
                CD3DX12_RESOURCE_BARRIER::Transition( sumLuminanceBuffer1->GetBuffer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE );
                CD3DX12_RESOURCE_BARRIER::Transition( sumLuminanceBuffer0->GetBuffer(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
            };
            commandList->ResourceBarrier( iteration == 0 ? 1 : 2, barriers ); // The 1st iteration relies on state implicit promotion
        }

        descriptorTable = descriptorHeap->AllocateRange( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2 );
        commandList->SetComputeRootDescriptorTable( 1, descriptorTable.GPU );
        SRV = descriptorTable;
        UAV = descriptorTable.Offsetted( 1, D3D12Adapter::GetDescriptorSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ) );
        D3D12Adapter::GetDevice()->CopyDescriptorsSimple( 1, SRV.CPU, sumLuminanceBuffer1->GetSRV().CPU, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
        D3D12Adapter::GetDevice()->CopyDescriptorsSimple( 1, UAV.CPU, sumLuminanceBuffer0->GetUAV().CPU, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

        uint32_t threadGroupCount = uint32_t( std::ceilf( blockCount / float( SL_REDUCE_TO_SINGLE_GROUPTHREADS ) ) );

        // Allocate a constant buffer
        {
            uint32_t params[ 4 ];
            params[ 0 ] = blockCount;
            params[ 1 ] = threadGroupCount;
            CD3D12ResourcePtr<GPUBuffer> constantBuffer( GPUBuffer::Create( sizeof( uint32_t ) * 4, 0, DXGI_FORMAT_UNKNOWN, 
                EGPUBufferUsage::Dynamic, EGPUBufferBindFlag_ConstantBuffer, params ) );

            commandList->SetComputeRootConstantBufferView( 0, constantBuffer->GetGPUVirtualAddress() );
        }

        commandList->Dispatch( threadGroupCount, 1, 1 );

        blockCount = threadGroupCount;

        std::swap( sumLuminanceBuffer0, sumLuminanceBuffer1 );

        ++iteration;
    }

    m_LuminanceResultBuffer = sumLuminanceBuffer1;
}

void SceneLuminanceRenderer::OnImGUI()
{
}
