#include "stdafx.h"
#include "SampleConvolutionRenderer.h"
#include "D3D12Adapter.h"
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
    CD3DX12_ROOT_PARAMETER1 rootParameters[ 4 ];
    rootParameters[ 0 ].InitAsConstantBufferView( 0 );
    rootParameters[ 1 ].InitAsShaderResourceView( 0 );
    rootParameters[ 2 ].InitAsShaderResourceView( 1 );
    rootParameters[ 3 ].InitAsUnorderedAccessView( 0 );
    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc( 3, rootParameters );

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

        GPUBufferPtr m_ConstantBuffer( GPUBuffer::Create( sizeof( SConvolutionConstant ), 0, DXGI_FORMAT_UNKNOWN, EGPUBufferUsage::Dynamic,
            EGPUBufferBindFlag_ConstantBuffer, &constant ) );
        
        commandList->SetComputeRootConstantBufferView( 0, constantBuffer->GetGPUVirtualAddress() );
    }

    commandList->SetComputeRootShaderResourceView( 1, scene.m_SamplePositionTexture->GetSRV().GPU.ptr );
    commandList->SetComputeRootShaderResourceView( 2, scene.m_SampleValueTexture->GetSRV().GPU.ptr );
    commandList->SetComputeRootUnorderedAccessView( 3, scene.m_FilmTexture->GetUAV().GPU.ptr );
    commandList->SetPipelineState( m_PSO.get() );

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



