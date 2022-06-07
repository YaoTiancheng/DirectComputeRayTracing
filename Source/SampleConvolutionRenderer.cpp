#include "stdafx.h"
#include "SampleConvolutionRenderer.h"
#include "Scene.h"
#include "Shader.h"
#include "GPUBuffer.h"
#include "RenderContext.h"
#include "RenderData.h"
#include "ComputeJob.h"
#include "GPUTexture.h"
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
    m_ConstantBuffer.reset( GPUBuffer::Create(
          sizeof( SConvolutionConstant )
        , 0
        , DXGI_FORMAT_UNKNOWN
        , D3D11_USAGE_DYNAMIC
        , D3D11_BIND_CONSTANT_BUFFER
        , GPUResourceCreationFlags_CPUWriteable ) );
    if ( !m_ConstantBuffer )
        return false;

    return true;
}

void CSampleConvolutionRenderer::Execute( const SRenderContext& renderContext, const CScene& scene, const SRenderData& renderData )
{
    SCOPED_RENDER_ANNOTATION( L"Convolute samples" );

    int32_t filterIndex = (int32_t)scene.m_Filter;
    if ( filterIndex != m_FilterIndex )
    {
        CompileShader( filterIndex );
        m_FilterIndex = filterIndex;
    }

    if ( void* address = m_ConstantBuffer->Map() )
    {
        SConvolutionConstant* constant = (SConvolutionConstant*)address;
        constant->g_Resolution[ 0 ] = renderContext.m_CurrentResolutionWidth;
        constant->g_Resolution[ 1 ] = renderContext.m_CurrentResolutionHeight;
        constant->g_FilterRadius = scene.m_FilterRadius;
        if ( scene.m_Filter == EFilter::Gaussian )
        {
            constant->g_GaussianAlpha = scene.m_GaussianFilterAlpha;
            constant->g_GaussianExp = std::exp( -scene.m_GaussianFilterAlpha * scene.m_FilterRadius * scene.m_FilterRadius );
        }
        else if ( scene.m_Filter == EFilter::Mitchell )
        {
            const float B = scene.m_MitchellB;
            const float C = scene.m_MitchellC;
            constant->g_MitchellFactor0 = -B - 6 * C;
            constant->g_MitchellFactor1 = 6 * B + 30 * C;
            constant->g_MitchellFactor2 = -12 * B - 48 * C;
            constant->g_MitchellFactor3 = 8 * B + 24 * C;
            constant->g_MitchellFactor4 = 12 - 9 * B - 6 * C;
            constant->g_MitchellFactor5 = -18 + 12 * B + 6 * C;
            constant->g_MitchellFactor6 = 6 - 2 * B;
        }
        else if ( scene.m_Filter == EFilter::LanczosSinc )
        {
            constant->g_LanczosSincTau = scene.m_LanczosSincTau;
        }
        m_ConstantBuffer->Unmap();
    }

    ComputeJob computeJob;
    computeJob.m_ConstantBuffers = { m_ConstantBuffer->GetBuffer() };
    computeJob.m_SRVs = { renderData.m_SamplePositionTexture->GetSRV(), renderData.m_SampleValueTexture->GetSRV() };
    computeJob.m_UAVs = { renderData.m_FilmTexture->GetUAV() };
    computeJob.m_Shader = m_Shader.get();
    computeJob.m_DispatchSizeX = renderContext.m_CurrentResolutionWidth / 8;
    computeJob.m_DispatchSizeX += renderContext.m_CurrentResolutionWidth % 8 ? 1 : 0;
    computeJob.m_DispatchSizeY = renderContext.m_CurrentResolutionHeight / 8;
    computeJob.m_DispatchSizeY += renderContext.m_CurrentResolutionHeight % 8 ? 1 : 0;
    computeJob.m_DispatchSizeZ = 1;
    computeJob.Dispatch();
}

void CSampleConvolutionRenderer::CompileShader( int32_t filterIndex )
{
    const char* filterDefines[] = { "FILTER_BOX", "FILTER_TRIANGLE", "FILTER_GAUSSIAN", "FILTER_MITCHELL", "FILTER_LANCZOS_SINC" };
    std::vector<D3D_SHADER_MACRO> shaderDefines;
    assert( filterIndex < ARRAY_LENGTH( filterDefines ) );
    shaderDefines.push_back( { filterDefines[ filterIndex ], "" } );
    shaderDefines.push_back( { NULL, NULL } );
    m_Shader.reset( ComputeShader::CreateFromFile( L"Shaders\\SampleConvolution.hlsl", shaderDefines ) );
}



