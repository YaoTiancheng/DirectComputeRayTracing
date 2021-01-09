#include "stdafx.h"
#include "PostProcessingRenderer.h"
#include "D3D11RenderSystem.h"
#include "GPUTexture.h"
#include "GPUBuffer.h"
#include "Shader.h"
#include "imgui/imgui.h"

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

PostProcessingRenderer::PostProcessingRenderer()
    : m_IsConstantBufferDirty( false )
    , m_IsPostFXDisabled( false )
    , m_IsJobDirty( true )
{
}

bool PostProcessingRenderer::Init( uint32_t renderWidth, uint32_t renderHeight, const GPUTexturePtr& filmTexture, const GPUBufferPtr& luminanceBuffer )
{
    std::vector<D3D_SHADER_MACRO> shaderDefines;
    shaderDefines.push_back( { NULL, NULL } );
    m_Shader.reset( GfxShader::CreateFromFile( L"Shaders\\PostProcessings.hlsl", shaderDefines ) );
    if ( !m_Shader )
        return false;

    shaderDefines.clear();
    shaderDefines.push_back( { "DISABLE_POST_FX", "0" } );
    shaderDefines.push_back( { NULL, NULL } );
    m_ShaderDisablePostFX.reset( GfxShader::CreateFromFile( L"Shaders\\PostProcessings.hlsl", shaderDefines ) );
    if ( !m_ShaderDisablePostFX )
        return false;

    m_ConstantParams = XMFLOAT4( 1.0f / ( renderWidth * renderHeight ), 0.7f, 0.0f, 0.0f );
    m_ConstantsBuffer.reset( GPUBuffer::Create(
          sizeof( XMFLOAT4 )
        , 0
        , GPUResourceCreationFlags_CPUWriteable | GPUResourceCreationFlags_IsConstantBuffer
        , &m_ConstantParams ) );
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
    samplerDesc.Filter   = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MaxAnisotropy = 1;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    HRESULT hr = GetDevice()->CreateSamplerState( &samplerDesc, &m_CopySamplerState );
    if ( FAILED( hr ) )
        return false;

    m_PostProcessingJob.m_SamplerStates.push_back( m_CopySamplerState.Get() );
    m_PostProcessingJob.m_ConstantBuffers.push_back( m_ConstantsBuffer->GetBuffer() );
    m_PostProcessingJob.m_SRVs.push_back( filmTexture->GetSRV() );
    m_PostProcessingJob.m_SRVs.push_back( luminanceBuffer->GetSRV() );
    m_PostProcessingJob.m_VertexBuffer = m_ScreenQuadVerticesBuffer.get();
    m_PostProcessingJob.m_InputLayout = m_ScreenQuadVertexInputLayout.Get();
    m_PostProcessingJob.m_VertexCount = 6;
    m_PostProcessingJob.m_VertexStride = sizeof( XMFLOAT4 );

    m_IsJobDirty = true;

    return true;
}

void PostProcessingRenderer::Execute()
{
    if ( m_IsConstantBufferDirty )
    {
        if ( void* address = m_ConstantsBuffer->Map() )
        {
            memcpy( address, &m_ConstantParams, sizeof( m_ConstantParams ) );
            m_ConstantsBuffer->Unmap();
            m_IsConstantBufferDirty = false;
        }
    }

    if ( m_IsJobDirty )
    {
        UpdateJob();
        m_IsJobDirty = false;
    }

    m_PostProcessingJob.Dispatch();
}

bool PostProcessingRenderer::OnImGUI()
{
    bool hasPropertyChanged = false;

    if ( ImGui::CollapsingHeader( "Post Processing" ) )
    {
        hasPropertyChanged = hasPropertyChanged || ImGui::DragFloat( "Luminance White(^2)", (float*)&m_ConstantParams.y, 0.01f, 0.001f, 1000.0f );
    }

    m_IsConstantBufferDirty = hasPropertyChanged;

    return hasPropertyChanged;
}

void PostProcessingRenderer::UpdateJob()
{
    m_PostProcessingJob.m_Shader = m_IsPostFXDisabled ? m_ShaderDisablePostFX.get() : m_Shader.get();
}
