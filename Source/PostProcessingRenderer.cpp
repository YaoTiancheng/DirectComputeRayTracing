#include "stdafx.h"
#include "PostProcessingRenderer.h"
#include "D3D11RenderSystem.h"
#include "GPUTexture.h"
#include "GPUBuffer.h"
#include "Shader.h"

using namespace DirectX;

XMFLOAT4 s_ScreenQuadVertices[ 6 ] =
{
    { -1.0f,  1.0f,  0.0f,  1.0f },
    {  1.0f,  1.0f,  0.0f,  1.0f },
    { -1.0f, -1.0f,  0.0f,  1.0f },
    { -1.0f, -1.0f,  0.0f,  1.0f },
    {  1.0f,  1.0f,  0.0f,  1.0f },
    {  1.0f, -1.0f,  0.0f,  1.0f },
};

D3D11_INPUT_ELEMENT_DESC s_ScreenQuadInputElementDesc[ 1 ]
{
    { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
};

bool PostProcessingRenderer::Init( uint32_t resolutionWidth, uint32_t resolutionHeight, const GPUTexturePtr& filmTexture, const GPUBufferPtr& luminanceBuffer )
{
    std::vector<D3D_SHADER_MACRO> shaderDefines;
    shaderDefines.push_back( { NULL, NULL } );
    m_Shader.reset( GfxShader::CreateFromFile( L"Shaders\\PostProcessings.hlsl", shaderDefines ) );
    if ( !m_Shader )
        return false;

    XMFLOAT4 params = XMFLOAT4( 1.0f / ( resolutionWidth * resolutionHeight ), 0.0f, 0.0f, 0.0f );
    m_ConstantsBuffer.reset( GPUBuffer::Create(
          sizeof( XMFLOAT4 )
        , 0
        , GPUResourceCreationFlags_IsImmutable | GPUResourceCreationFlags_IsConstantBuffer
        , &params ) );
    if ( !m_ConstantsBuffer )
        return false;

    m_ScreenQuadVerticesBuffer.reset( GPUBuffer::Create(
          sizeof( s_ScreenQuadVertices )
        , sizeof( XMFLOAT4 )
        , GPUResourceCreationFlags_IsImmutable | GPUResourceCreationFlags_IsVertexBuffer
        , &s_ScreenQuadVertices ) );
    if ( !m_ScreenQuadVerticesBuffer )
        return false;

    m_ScreenQuadVertexInputLayout.Attach( m_Shader->CreateInputLayout( s_ScreenQuadInputElementDesc, 1 ) );
    if ( !m_ScreenQuadVertexInputLayout )
        return false;

    D3D11_SAMPLER_DESC samplerDesc;
    ZeroMemory( &samplerDesc, sizeof( D3D11_SAMPLER_DESC ) );
    samplerDesc.Filter   = D3D11_FILTER_MAXIMUM_MIN_MAG_MIP_POINT;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MaxAnisotropy = 1;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    HRESULT hr = GetDevice()->CreateSamplerState( &samplerDesc, &m_CopySamplerState );
    if ( FAILED( hr ) )
        return false;

    m_DefaultViewport = { 0.0f, 0.0f, (float)resolutionWidth, (float)resolutionHeight, 0.0f, 1.0f };

    m_PostProcessingJob.m_SamplerStates.push_back( m_CopySamplerState.Get() );
    m_PostProcessingJob.m_SRVs.push_back( filmTexture->GetSRV() );
    m_PostProcessingJob.m_SRVs.push_back( luminanceBuffer->GetSRV() );
    m_PostProcessingJob.m_ConstantBuffers.push_back( m_ConstantsBuffer->GetBuffer() );
    m_PostProcessingJob.m_Shader = m_Shader.get();
    m_PostProcessingJob.m_VertexBuffer = m_ScreenQuadVerticesBuffer.get();
    m_PostProcessingJob.m_InputLayout = m_ScreenQuadVertexInputLayout.Get();
    m_PostProcessingJob.m_VertexCount = 6;
    m_PostProcessingJob.m_VertexStride = sizeof( XMFLOAT4 );

    return true;
}

void PostProcessingRenderer::Execute( const std::unique_ptr<GPUTexture>& renderTargetTexture )
{
    ID3D11DeviceContext* deviceContext = GetDeviceContext();

    ID3D11RenderTargetView* rawDefaultRenderTargetView = renderTargetTexture->GetRTV();
    deviceContext->OMSetRenderTargets( 1, &rawDefaultRenderTargetView, nullptr );

    deviceContext->RSSetViewports( 1, &m_DefaultViewport );

    m_PostProcessingJob.Dispatch();
}
