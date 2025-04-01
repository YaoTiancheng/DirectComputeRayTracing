#include "stdafx.h"
#include "DirectComputeRayTracing.h"
#include "D3D12Adapter.h"
#include "D3D12DescriptorUtil.h"
#include "Logging.h"
#include "Shader.h"
#include "GPUBuffer.h"
#include "GPUTexture.h"
#include "RenderContext.h"
#include "ScopedRenderAnnotation.h"

using namespace D3D12Util;

struct alignas( 256 ) SConvolutionConstant
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
};

static SD3D12DescriptorTableLayout s_DescriptorTableLayout = SD3D12DescriptorTableLayout( 2, 1 );

bool SRenderer::InitSampleConvolution()
{
    CD3DX12_ROOT_PARAMETER1 rootParameters[ 2 ];
    rootParameters[ 0 ].InitAsConstantBufferView( 0 );
    SD3D12DescriptorTableRanges descriptorTableRanges;
    s_DescriptorTableLayout.InitRootParameter( &rootParameters[ 1 ], &descriptorTableRanges );
    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc( 2, rootParameters );

    ComPtr<ID3DBlob> serializedRootSignature;
    ComPtr<ID3DBlob> error;
    HRESULT hr = D3D12SerializeVersionedRootSignature( &rootSignatureDesc, serializedRootSignature.GetAddressOf(), error.GetAddressOf() ); 
    if ( error )
    {
        LOG_STRING_FORMAT( "Create sample convolution root signature with error: %s\n", (const char*)error->GetBufferPointer() );
    }
    if ( FAILED( hr ) )
    {
        return false;
    }

    if ( FAILED( D3D12Adapter::GetDevice()->CreateRootSignature( 0, serializedRootSignature->GetBufferPointer(), serializedRootSignature->GetBufferSize(),
        IID_PPV_ARGS( m_SampleConvolutionRootSignature.GetAddressOf() ) ) ) )
    {
        return false;
    }

    return true;
}

static void CompileShader( SRenderer* r, int32_t filterIndex )
{
    // Compile shader
    const wchar_t* filterDefines[] = { L"FILTER_BOX", L"FILTER_TRIANGLE", L"FILTER_GAUSSIAN", L"FILTER_MITCHELL", L"FILTER_LANCZOS_SINC" };
    std::vector<DxcDefine> shaderDefines;
    assert( filterIndex < ARRAY_LENGTH( filterDefines ) );
    shaderDefines.push_back( { filterDefines[ filterIndex ], L"" } );
    ComputeShaderPtr shader( ComputeShader::CreateFromFile( L"Shaders\\SampleConvolution.hlsl", shaderDefines ) );
    if ( !shader )
    {
        return;
    }

    // Create PSOs
    D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = r->m_SampleConvolutionRootSignature.Get();
    desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    desc.CS = shader->GetShaderBytecode();
    ID3D12PipelineState* PSO = nullptr;
    if ( FAILED( D3D12Adapter::GetDevice()->CreateComputePipelineState( &desc, IID_PPV_ARGS( &PSO ) ) ) )
    {
        return;
    }

    r->m_SampleConvolutionPSO.reset( PSO, SD3D12ComDeferredDeleter() );
}

void SRenderer::ExecuteSampleConvolution( const SRenderContext& renderContext )
{
    ID3D12GraphicsCommandList* commandList = D3D12Adapter::GetCommandList();

    SCOPED_RENDER_ANNOTATION( commandList, L"Convolute samples" );

    int32_t filterIndex = (int32_t)m_Scene.m_Filter;
    if ( filterIndex != m_SampleConvolutionFilterIndex )
    {
        CompileShader( this, filterIndex );
        m_SampleConvolutionFilterIndex = filterIndex;
    }

    commandList->SetComputeRootSignature( m_SampleConvolutionRootSignature.Get() );

    // Allocate constant buffer
    {
        SConvolutionConstant constant;
        constant.g_Resolution[ 0 ] = renderContext.m_CurrentResolutionWidth;
        constant.g_Resolution[ 1 ] = renderContext.m_CurrentResolutionHeight;
        constant.g_FilterRadius = m_Scene.m_FilterRadius;
        if ( m_Scene.m_Filter == EFilter::Gaussian )
        {
            constant.g_GaussianAlpha = m_Scene.m_GaussianFilterAlpha;
            constant.g_GaussianExp = std::exp( -m_Scene.m_GaussianFilterAlpha * m_Scene.m_FilterRadius * m_Scene.m_FilterRadius );
        }
        else if ( m_Scene.m_Filter == EFilter::Mitchell )
        {
            const float B = m_Scene.m_MitchellB;
            const float C = m_Scene.m_MitchellC;
            constant.g_MitchellFactor0 = -B - 6 * C;
            constant.g_MitchellFactor1 = 6 * B + 30 * C;
            constant.g_MitchellFactor2 = -12 * B - 48 * C;
            constant.g_MitchellFactor3 = 8 * B + 24 * C;
            constant.g_MitchellFactor4 = 12 - 9 * B - 6 * C;
            constant.g_MitchellFactor5 = -18 + 12 * B + 6 * C;
            constant.g_MitchellFactor6 = 6 - 2 * B;
        }
        else if ( m_Scene.m_Filter == EFilter::LanczosSinc )
        {
            constant.g_LanczosSincTau = m_Scene.m_LanczosSincTau;
        }

        CD3D12ResourcePtr<GPUBuffer> constantBuffer( GPUBuffer::Create( sizeof( SConvolutionConstant ), 0, DXGI_FORMAT_UNKNOWN, EGPUBufferUsage::Dynamic,
            EGPUBufferBindFlag_ConstantBuffer, &constant ) );
        
        commandList->SetComputeRootConstantBufferView( 0, constantBuffer->GetGPUVirtualAddress() );
    }

    // Barriers
    {
        D3D12_RESOURCE_BARRIER barriers[ 3 ];
        uint32_t barriersCount = 0;

        barriers[ barriersCount++ ] = CD3DX12_RESOURCE_BARRIER::Transition( m_Scene.m_FilmTexture->GetTexture(), m_Scene.m_FilmTextureStates, D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
        m_Scene.m_FilmTextureStates = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        if ( !m_Scene.m_IsSampleTexturesRead )
        {
            barriers[ barriersCount++ ] = CD3DX12_RESOURCE_BARRIER::Transition( m_Scene.m_SamplePositionTexture->GetTexture(),
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE );
            barriers[ barriersCount++ ] = CD3DX12_RESOURCE_BARRIER::Transition( m_Scene.m_SampleValueTexture->GetTexture(),
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE );
            m_Scene.m_IsSampleTexturesRead = true;
        }

        commandList->ResourceBarrier( barriersCount, barriers );
    }

    SD3D12DescriptorHandle SRVs[] = { m_Scene.m_SamplePositionTexture->GetSRV(), m_Scene.m_SampleValueTexture->GetSRV() };
    D3D12_GPU_DESCRIPTOR_HANDLE descriptorTable = s_DescriptorTableLayout.AllocateAndCopyToGPUDescriptorHeap( SRVs, (uint32_t)ARRAY_LENGTH( SRVs ), &m_Scene.m_FilmTexture->GetUAV(), 1 );
    commandList->SetComputeRootDescriptorTable( 1, descriptorTable );

    commandList->SetPipelineState( m_SampleConvolutionPSO.get() );

    uint32_t dispatchSizeX = renderContext.m_CurrentResolutionWidth / 8;
    dispatchSizeX += renderContext.m_CurrentResolutionWidth % 8 ? 1 : 0;
    uint32_t dispatchSizeY = renderContext.m_CurrentResolutionHeight / 8;
    dispatchSizeY += renderContext.m_CurrentResolutionHeight % 8 ? 1 : 0;
    commandList->Dispatch( dispatchSizeX, dispatchSizeY, 1 );
}