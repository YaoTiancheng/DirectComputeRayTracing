#include "stdafx.h"
#include "PostProcessingRenderer.h"
#include "D3D11RenderSystem.h"
#include "GPUTexture.h"
#include "GPUBuffer.h"
#include "Shader.h"
#include "RenderContext.h"
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
    : m_IsPostFXEnabled( true )
{
}

bool PostProcessingRenderer::Init( uint32_t renderWidth, uint32_t renderHeight, const GPUTexturePtr& filmTexture, const GPUTexturePtr& renderResultTexture, const GPUBufferPtr& luminanceBuffer )
{
    std::vector<D3D_SHADER_MACRO> shaderDefines;
    shaderDefines.push_back( { NULL, NULL } );
    m_PostFXShader.reset( GfxShader::CreateFromFile( L"Shaders\\PostProcessings.hlsl", shaderDefines ) );
    if ( !m_PostFXShader )
        return false;

    shaderDefines.clear();
    shaderDefines.push_back( { "DISABLE_POST_FX", "0" } );
    shaderDefines.push_back( { NULL, NULL } );
    m_PostFXDisabledShader.reset( GfxShader::CreateFromFile( L"Shaders\\PostProcessings.hlsl", shaderDefines ) );
    if ( !m_PostFXDisabledShader )
        return false;

    shaderDefines.clear();
    shaderDefines.push_back( { "COPY", "0" } );
    shaderDefines.push_back( { NULL, NULL } );
    m_CopyShader.reset( GfxShader::CreateFromFile( L"Shaders\\PostProcessings.hlsl", shaderDefines ) );
    if ( !m_CopyShader )
        return false;

    m_ConstantParams = XMFLOAT4( 1.0f / ( renderWidth * renderHeight ), 0.7f, 1.0f, 0.0f );
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

    m_ScreenQuadVertexInputLayout.Attach( m_PostFXShader->CreateInputLayout( s_ScreenQuadInputElementDesc, 1 ) );
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
    HRESULT hr = GetDevice()->CreateSamplerState( &samplerDesc, &m_LinearSamplerState );
    if ( FAILED( hr ) )
        return false;

    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    hr = GetDevice()->CreateSamplerState( &samplerDesc, &m_PointSamplerState );
    if ( FAILED( hr ) )
        return false;

    m_PostFXJob.m_SamplerStates.push_back( m_PointSamplerState.Get() );
    m_PostFXJob.m_ConstantBuffers.push_back( m_ConstantsBuffer->GetBuffer() );
    m_PostFXJob.m_SRVs.push_back( filmTexture->GetSRV() );
    m_PostFXJob.m_SRVs.push_back( luminanceBuffer->GetSRV() );
    m_PostFXJob.m_VertexBuffer = m_ScreenQuadVerticesBuffer.get();
    m_PostFXJob.m_InputLayout = m_ScreenQuadVertexInputLayout.Get();
    m_PostFXJob.m_VertexCount = 6;
    m_PostFXJob.m_VertexStride = sizeof( XMFLOAT4 );

    m_CopyJob.m_SamplerStates.push_back( m_LinearSamplerState.Get() );
    m_CopyJob.m_ConstantBuffers.push_back( m_ConstantsBuffer->GetBuffer() );
    m_CopyJob.m_SRVs.push_back( renderResultTexture->GetSRV() );
    m_CopyJob.m_VertexBuffer = m_ScreenQuadVerticesBuffer.get();
    m_CopyJob.m_InputLayout = m_ScreenQuadVertexInputLayout.Get();
    m_CopyJob.m_VertexCount = 6;
    m_CopyJob.m_VertexStride = sizeof( XMFLOAT4 );
    m_CopyJob.m_Shader = m_CopyShader.get();

    return true;
}

void PostProcessingRenderer::ExecutePostFX( const SRenderContext& renderContext )
{
    if ( void* address = m_ConstantsBuffer->Map() )
    {
        m_ConstantParams.x = 1.0f / ( renderContext.m_CurrentResolutionWidth * renderContext.m_CurrentResolutionHeight );
        m_ConstantParams.z = renderContext.m_CurrentResolutionRatio;
        memcpy( address, &m_ConstantParams, sizeof( m_ConstantParams ) );
        m_ConstantsBuffer->Unmap();
    }

    UpdateJob( renderContext.m_EnablePostFX );
        
    m_PostFXJob.Dispatch();
}

void PostProcessingRenderer::ExecuteCopy()
{
    if ( void* address = m_ConstantsBuffer->Map() )
    {
        m_ConstantParams.z = 1.0f;
        memcpy( address, &m_ConstantParams, sizeof( m_ConstantParams ) );
        m_ConstantsBuffer->Unmap();
    }

    m_CopyJob.Dispatch();
}

bool PostProcessingRenderer::OnImGUI()
{
    bool hasPropertyChanged = false;

    if ( ImGui::CollapsingHeader( "Post Processing" ) )
    {
        ImGui::Checkbox( "Enabled", &m_IsPostFXEnabled );

        if ( m_IsPostFXEnabled )
        {
            hasPropertyChanged = hasPropertyChanged || ImGui::DragFloat( "Luminance White(^2)", (float*)&m_ConstantParams.y, 0.01f, 0.001f, 1000.0f );
        }
    }

    return hasPropertyChanged;
}

void PostProcessingRenderer::UpdateJob( bool enablePostFX )
{
    m_PostFXJob.m_Shader = !m_IsPostFXEnabled || !enablePostFX ? m_PostFXDisabledShader.get() : m_PostFXShader.get();
}
