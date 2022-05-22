#include "stdafx.h"
#include "PostProcessingRenderer.h"
#include "D3D11RenderSystem.h"
#include "GPUTexture.h"
#include "GPUBuffer.h"
#include "Shader.h"
#include "RenderContext.h"
#include "ScopedRenderAnnotation.h"
#include "Scene.h"
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

struct SConstant
{
    float m_ReciprocalPixelCount;
    float m_MaxWhiteSqr;
    float m_TexcoordScale;
    float m_EV100;
};

namespace
{
    float CalculateEV100( float relativeAperture, float shutterTime, float ISO )
    {
        return std::log2( relativeAperture * relativeAperture / shutterTime * 100 / ISO );
    }
}

PostProcessingRenderer::PostProcessingRenderer()
    : m_IsPostFXEnabled( true )
    , m_IsAutoExposureEnabled( true )
    , m_LuminanceWhite( 1.0f )
    , m_CalculateEV100FromCamera( true )
    , m_ManualEV100( 15.f )
{
}

bool PostProcessingRenderer::Init( uint32_t renderWidth, uint32_t renderHeight, const GPUTexturePtr& filmTexture, const GPUTexturePtr& renderResultTexture )
{
    if ( !m_LuminanceRenderer.Init( renderWidth, renderHeight, filmTexture ) )
    {
        return false;
    }

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
    shaderDefines.push_back( { "AUTO_EXPOSURE", "0" } );
    shaderDefines.push_back( { NULL, NULL } );
    m_PostFXAutoExposureShader.reset( GfxShader::CreateFromFile( L"Shaders\\PostProcessings.hlsl", shaderDefines ) );
    if ( !m_PostFXAutoExposureShader )
        return false;

    shaderDefines.clear();
    shaderDefines.push_back( { "COPY", "0" } );
    shaderDefines.push_back( { NULL, NULL } );
    m_CopyShader.reset( GfxShader::CreateFromFile( L"Shaders\\PostProcessings.hlsl", shaderDefines ) );
    if ( !m_CopyShader )
        return false;

    m_ConstantsBuffer.reset( GPUBuffer::Create(
          sizeof( SConstant )
        , 0
        , DXGI_FORMAT_UNKNOWN
        , D3D11_USAGE_DYNAMIC
        , D3D11_BIND_CONSTANT_BUFFER
        , GPUResourceCreationFlags_CPUWriteable ) );
    if ( !m_ConstantsBuffer )
        return false;

    m_ScreenQuadVerticesBuffer.reset( GPUBuffer::Create(
          sizeof( s_ScreenQuadVertices )
        , sizeof( XMFLOAT4 )
        , DXGI_FORMAT_UNKNOWN
        , D3D11_USAGE_IMMUTABLE
        , D3D11_BIND_VERTEX_BUFFER
        , 0
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
    m_PostFXJob.m_SRVs.push_back( nullptr );
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

void PostProcessingRenderer::ExecuteLuminanceCompute( const SRenderContext& renderContext )
{
    if ( m_IsAutoExposureEnabled && m_IsPostFXEnabled && renderContext.m_EnablePostFX )
    {
        m_LuminanceRenderer.Dispatch( renderContext.m_CurrentResolutionWidth, renderContext.m_CurrentResolutionHeight );
    }
}

void PostProcessingRenderer::ExecutePostFX( const SRenderContext& renderContext, const CScene& scene )
{
    SCOPED_RENDER_ANNOTATION( L"PostFX" );

    if ( void* address = m_ConstantsBuffer->Map() )
    {
        SConstant* constants = (SConstant*)address;
        constants->m_ReciprocalPixelCount = 1.0f / ( renderContext.m_CurrentResolutionWidth * renderContext.m_CurrentResolutionHeight );
        constants->m_MaxWhiteSqr = m_LuminanceWhite * m_LuminanceWhite;
        constants->m_TexcoordScale = renderContext.m_CurrentResolutionRatio;
        constants->m_EV100 = m_CalculateEV100FromCamera ? CalculateEV100( scene.m_RelativeAperture, scene.m_ShutterTime, scene.m_ISO ) : m_ManualEV100;
        m_ConstantsBuffer->Unmap();
    }

    UpdateJob( renderContext.m_EnablePostFX );
        
    m_PostFXJob.Dispatch();
}

void PostProcessingRenderer::ExecuteCopy()
{
    SCOPED_RENDER_ANNOTATION( L"Copy" );

    if ( void* address = m_ConstantsBuffer->Map() )
    {
        SConstant* constants = (SConstant*)address;
        constants->m_TexcoordScale = 1.0f;
        m_ConstantsBuffer->Unmap();
    }

    m_CopyJob.Dispatch();
}

bool PostProcessingRenderer::OnImGUI()
{
    if ( ImGui::CollapsingHeader( "Post Processing" ) )
    {
        ImGui::Checkbox( "Enabled", &m_IsPostFXEnabled );
        if ( m_IsPostFXEnabled )
        {
            ImGui::Checkbox( "Auto Exposure", &m_IsAutoExposureEnabled );
            if ( !m_IsAutoExposureEnabled )
            {
                ImGui::Checkbox( "EV100 From Camera Setting", &m_CalculateEV100FromCamera );
                if ( !m_CalculateEV100FromCamera )
                {
                    ImGui::DragFloat( "EV100", &m_ManualEV100, 1.0f, -100.0f, 100.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp );
                }
            }
            ImGui::DragFloat( "Luminance White", &m_LuminanceWhite, 0.01f, 0.001f, 1000.0f );
        }
    }

    return false;
}

void PostProcessingRenderer::UpdateJob( bool enablePostFX )
{
    m_PostFXJob.m_Shader = !m_IsPostFXEnabled || !enablePostFX ? m_PostFXDisabledShader.get() : ( m_IsAutoExposureEnabled ? m_PostFXAutoExposureShader.get() : m_PostFXShader.get() );
    m_PostFXJob.m_SRVs[ 1 ] = m_LuminanceRenderer.GetLuminanceResultBuffer()->GetSRV();
}
