#include "stdafx.h"
#include "SampleConvolutionRenderer.h"
#include "D3D12Adapter.h"
#include "D3D12GPUDescriptorHeap.h"
#include "Logging.h"
#include "Shader.h"
#include "GPUBuffer.h"
#include "GPUTexture.h"
#include "Scene.h"
#include "RenderContext.h"
#include "ScopedRenderAnnotation.h"

struct SConvolutionConstant
{
    uint32_t g_Resolution[ 2 ];
    float g_FilterRadius;
    float g_GaussianAlpha;
    float g_GaussianExp;
    float g_MitchellFactor0;
    float g_MitchellFactor1;
    float g_MitchellFactor2;
    float g_MitchellFactor3;
    float g_MitchellFactor4;
    float g_MitchellFactor5;
    float g_MitchellFactor6;
    uint32_t g_LanczosSincTau;
    uint32_t padding[ 3 ];
};

bool CSampleConvolutionRenderer::Init()
{
    CD3DX12_ROOT_PARAMETER1 rootParameters[ 2 ];
    rootParameters[ 0 ].InitAsConstantBufferView( 0 );
    CD3DX12_DESCRIPTOR_RANGE1 descriptorRanges[ 2 ];
    descriptorRanges[ 0 ].Init( D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0 );
    descriptorRanges[ 1 ].Init( D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0 );
    rootParameters[ 1 ].InitAsDescriptorTable( 2, descriptorRanges );
    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc( 2, rootParameters );

    ComPtr<ID3DBlob> serializedRootSignature;
    ComPtr<ID3DBlob> error;
    HRESULT hr = D3D12SerializeVersionedRootSignature( &rootSignatureDesc, serializedRootSignature.GetAddressOf(), error.GetAddressOf() ); 
    LOG_STRING_FORMAT( "Create sample convolution root signature with error: %s\n", (const char*)error->GetBufferPointer() );
    if ( FAILED( hr ) )
    {
        return false;
    }

    if ( FAILED( D3D12Adapter::GetDevice()->CreateRootSignature( 0, serializedRootSignature->GetBufferPointer(), serializedRootSignature->GetBufferSize(), IID_PPV_ARGS( m_RootSignature.GetAddressOf() ) ) ) )
    {
        return false;
    }

    return true;
}

void CSampleConvolutionRenderer::Execute( const SRenderContext& renderContext, const CScene& scene )
{
    SCOPED_RENDER_ANNOTATION( L"Convolute samples" );

    int32_t filterIndex = (int32_t)scene.m_Filter;
    if ( filterIndex != m_FilterIndex )
    {
        CompileShader( filterIndex );
        m_FilterIndex = filterIndex;
    }

    ID3D12GraphicsCommandList* commandList = D3D12Adapter::GetCommandList();

    commandList->SetComputeRootSignature( m_RootSignature.Get() );

    // Allocate constant buffer
    {
        SConvolutionConstant constant;
        constant.g_Resolution[ 0 ] = renderContext.m_CurrentResolutionWidth;
        constant.g_Resolution[ 1 ] = renderContext.m_CurrentResolutionHeight;
        constant.g_FilterRadius = scene.m_FilterRadius;
        if ( scene.m_Filter == EFilter::Gaussian )
        {
            constant.g_GaussianAlpha = scene.m_GaussianFilterAlpha;
            constant.g_GaussianExp = std::exp( -scene.m_GaussianFilterAlpha * scene.m_FilterRadius * scene.m_FilterRadius );
        }
        else if ( scene.m_Filter == EFilter::Mitchell )
        {
            const float B = scene.m_MitchellB;
            const float C = scene.m_MitchellC;
            constant.g_MitchellFactor0 = -B - 6 * C;
            constant.g_MitchellFactor1 = 6 * B + 30 * C;
            constant.g_MitchellFactor2 = -12 * B - 48 * C;
            constant.g_MitchellFactor3 = 8 * B + 24 * C;
            constant.g_MitchellFactor4 = 12 - 9 * B - 6 * C;
            constant.g_MitchellFactor5 = -18 + 12 * B + 6 * C;
            constant.g_MitchellFactor6 = 6 - 2 * B;
        }
        else if ( scene.m_Filter == EFilter::LanczosSinc )
        {
            constant.g_LanczosSincTau = scene.m_LanczosSincTau;
        }

        CD3D12ResourcePtr<GPUBuffer> constantBuffer( GPUBuffer::Create( sizeof( SConvolutionConstant ), 0, DXGI_FORMAT_UNKNOWN, EGPUBufferUsage::Dynamic,
            EGPUBufferBindFlag_ConstantBuffer, &constant ) );
        
        commandList->SetComputeRootConstantBufferView( 0, constantBuffer->GetGPUVirtualAddress() );
    }

    CD3D12DescritorHandle descriptorTable = D3D12Adapter::GetGPUDescriptorHeap( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV )->AllocateRange( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 3 );
    commandList->SetComputeRootDescriptorTable( 1, descriptorTable.GPU );
    D3D12Adapter::GetDevice()->CopyDescriptorsSimple( 1, descriptorTable.CPU, scene.m_SamplePositionTexture->GetSRV().CPU, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
    descriptorTable.Offset( 1, D3D12Adapter::GetDescriptorSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ) );
    D3D12Adapter::GetDevice()->CopyDescriptorsSimple( 1, descriptorTable.CPU, scene.m_SampleValueTexture->GetSRV().CPU, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
    descriptorTable.Offset( 1, D3D12Adapter::GetDescriptorSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ) );
    D3D12Adapter::GetDevice()->CopyDescriptorsSimple( 1, descriptorTable.CPU, scene.m_FilmTexture->GetUAV().CPU, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

    commandList->SetPipelineState( m_PSO.get() );

    // Barriers
    {
        D3D12_RESOURCE_BARRIER barriers[ 3 ];
        uint32_t barriersCount = 0;

        barriers[ barriersCount++ ] = CD3DX12_RESOURCE_BARRIER::Transition( scene.m_FilmTexture->GetTexture(),
            scene.m_IsFilmTextureCleared ? D3D12_RESOURCE_STATE_RENDER_TARGET : D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS );

        if ( !scene.m_IsSampleTexturesRead )
        {
            barriers[ barriersCount++ ] = CD3DX12_RESOURCE_BARRIER::Transition( scene.m_SamplePositionTexture->GetTexture(),
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE );
            barriers[ barriersCount++ ] = CD3DX12_RESOURCE_BARRIER::Transition( scene.m_SampleValueTexture->GetTexture(),
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE );
        }

        commandList->ResourceBarrier( barriersCount, &barrier );
    }

    uint32_t dispatchSizeX = renderContext.m_CurrentResolutionWidth / 8;
    dispatchSizeX += renderContext.m_CurrentResolutionWidth % 8 ? 1 : 0;
    uint32_t dispatchSizeY = renderContext.m_CurrentResolutionHeight / 8;
    dispatchSizeY += renderContext.m_CurrentResolutionHeight % 8 ? 1 : 0;
    commandList->Dispatch( dispatchSizeX, dispatchSizeY, 1 );
}

void CSampleConvolutionRenderer::CompileShader( int32_t filterIndex )
{
    // Compile shader
    const char* filterDefines[] = { "FILTER_BOX", "FILTER_TRIANGLE", "FILTER_GAUSSIAN", "FILTER_MITCHELL", "FILTER_LANCZOS_SINC" };
    std::vector<D3D_SHADER_MACRO> shaderDefines;
    assert( filterIndex < ARRAY_LENGTH( filterDefines ) );
    shaderDefines.push_back( { filterDefines[ filterIndex ], "" } );
    shaderDefines.push_back( { NULL, NULL } );
    ComputeShaderPtr shader( ComputeShader::CreateFromFile( L"Shaders\\SampleConvolution.hlsl", shaderDefines ) );
    if ( !shader )
    {
        return;
    }

    // Create PSOs
    D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = m_RootSignature.Get();
    desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    desc.CS = shader->GetShaderBytecode();
    ID3D12PipelineState* PSO = nullptr;
    if ( FAILED( D3D12Adapter::GetDevice()->CreateComputePipelineState( &desc, IID_PPV_ARGS( &PSO ) ) ) )
    {
        return;
    }

    m_PSO.reset( PSO, SD3D12ComDeferredDeleter() );
}



