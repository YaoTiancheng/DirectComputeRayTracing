#include "stdafx.h"
#include "PostProcessingRenderer.h"
#include "D3D12Adapter.h"
#include "D3D12DescriptorUtil.h"
#include "GPUBuffer.h"
#include "GPUTexture.h"
#include "Shader.h"
#include "RenderContext.h"
#include "ScopedRenderAnnotation.h"
#include "Scene.h"
#include "Logging.h"
#include "imgui/imgui.h"

using namespace DirectX;
using namespace D3D12Util;

XMFLOAT4 s_ScreenQuadVertices[ 6 ] =
{
    { -1.0f,  1.0f,  0.0f,  1.0f },
    {  1.0f,  1.0f,  0.0f,  1.0f },
    { -1.0f, -1.0f,  0.0f,  1.0f },
    { -1.0f, -1.0f,  0.0f,  1.0f },
    {  1.0f,  1.0f,  0.0f,  1.0f },
    {  1.0f, -1.0f,  0.0f,  1.0f },
};

struct SConvolutionConstant
{
    float m_ReciprocalPixelCount;
    float m_MaxWhiteSqr;
    float m_TexcoordScale;
    float m_EV100;
    uint8_t m_Padding[ 240 ];
};

static SD3D12DescriptorTableLayout s_DescriptorTableLayout = SD3D12DescriptorTableLayout( 2, 0 );

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

bool PostProcessingRenderer::Init()
{
    if ( !m_LuminanceRenderer.Init() )
    {
        return false;
    }

    // Compile shaders
    std::vector<D3D_SHADER_MACRO> shaderDefines;
    shaderDefines.push_back( { NULL, NULL } );
    GfxShaderPtr postFXShader( GfxShader::CreateFromFile( L"Shaders\\PostProcessings.hlsl", shaderDefines ) );
    if ( !postFXShader )
        return false;

    shaderDefines.clear();
    shaderDefines.push_back( { "DISABLE_POST_FX", "0" } );
    shaderDefines.push_back( { NULL, NULL } );
    GfxShaderPtr postFXDisabledShader( GfxShader::CreateFromFile( L"Shaders\\PostProcessings.hlsl", shaderDefines ) );
    if ( !postFXDisabledShader )
        return false;

    shaderDefines.clear();
    shaderDefines.push_back( { "AUTO_EXPOSURE", "0" } );
    shaderDefines.push_back( { NULL, NULL } );
    GfxShaderPtr postFXAutoExposureShader( GfxShader::CreateFromFile( L"Shaders\\PostProcessings.hlsl", shaderDefines ) );
    if ( !postFXAutoExposureShader )
        return false;

    shaderDefines.clear();
    shaderDefines.push_back( { "COPY", "0" } );
    shaderDefines.push_back( { NULL, NULL } );
    GfxShaderPtr copyShader( GfxShader::CreateFromFile( L"Shaders\\PostProcessings.hlsl", shaderDefines ) );
    if ( !copyShader )
        return false;

    // Static samplers
    D3D12_STATIC_SAMPLER_DESC samplers[ 2 ] = { {}, {} };
    for ( auto& sampler : samplers )
    {
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.MaxAnisotropy = 1;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }
    samplers[ 0 ].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[ 0 ].ShaderRegister = 0;
    samplers[ 1 ].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    samplers[ 1 ].ShaderRegister = 1;

    // Create root signature
    {
        CD3DX12_ROOT_PARAMETER1 rootParameters[ 2 ];
        rootParameters[ 0 ].InitAsConstantBufferView( 0 );
        SD3D12DescriptorTableRanges descriptorTableRanges;
        s_DescriptorTableLayout.InitRootParameter( &rootParameters[ 1 ], &descriptorTableRanges );
        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc( 2, rootParameters, 2, samplers, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT );

        ComPtr<ID3DBlob> serializedRootSignature;
        ComPtr<ID3DBlob> error;
        HRESULT hr = D3D12SerializeVersionedRootSignature( &rootSignatureDesc, serializedRootSignature.GetAddressOf(), error.GetAddressOf() ); 
        if ( error )
        {
            LOG_STRING_FORMAT( "Create post processing root signature with error: %s\n", (const char*)error->GetBufferPointer() );
        }
        if ( FAILED( hr ) )
        {
            return false;
        }

        if ( FAILED( D3D12Adapter::GetDevice()->CreateRootSignature( 0, serializedRootSignature->GetBufferPointer(), serializedRootSignature->GetBufferSize(), IID_PPV_ARGS( m_RootSignature.GetAddressOf() ) ) ) )
        {
            return false;
        }
    }

    D3D12_INPUT_ELEMENT_DESC screenQuadInputElementDesc[ 1 ]
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    // Create PSO
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { screenQuadInputElementDesc, 1 };
    psoDesc.pRootSignature = m_RootSignature.Get();
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC( D3D12_DEFAULT );
    psoDesc.BlendState = CD3DX12_BLEND_DESC( D3D12_DEFAULT );
    psoDesc.DepthStencilState.DepthEnable = false;
    psoDesc.DepthStencilState.StencilEnable = false;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[ 0 ] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

    // PostFX PSO
    psoDesc.VS = postFXShader->GetVertexShaderBytecode();
    psoDesc.PS = postFXShader->GetPixelShaderBytecode();
    if ( FAILED( D3D12Adapter::GetDevice()->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS( m_PostFXPSO.GetAddressOf() ) ) ) )
    {
        return false;
    }

    // PostFXAutoExposure PSO
    psoDesc.VS = postFXAutoExposureShader->GetVertexShaderBytecode();
    psoDesc.PS = postFXAutoExposureShader->GetPixelShaderBytecode();
    if ( FAILED( D3D12Adapter::GetDevice()->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS( m_PostFXAutoExposurePSO.GetAddressOf() ) ) ) )
    {
        return false;
    }

    // PostFXDisabled PSO
    psoDesc.VS = postFXDisabledShader->GetVertexShaderBytecode();
    psoDesc.PS = postFXDisabledShader->GetPixelShaderBytecode();
    if ( FAILED( D3D12Adapter::GetDevice()->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS( m_PostFXDisabledPSO.GetAddressOf() ) ) ) )
    {
        return false;
    }

    // Copy PSO
    psoDesc.VS = copyShader->GetVertexShaderBytecode();
    psoDesc.PS = copyShader->GetPixelShaderBytecode();
    if ( FAILED( D3D12Adapter::GetDevice()->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS( m_CopyPSO.GetAddressOf() ) ) ) )
    {
        return false;
    }

    m_ScreenQuadVerticesBuffer.reset( GPUBuffer::Create(
          sizeof( s_ScreenQuadVertices )
        , sizeof( XMFLOAT4 )
        , DXGI_FORMAT_UNKNOWN
        , EGPUBufferUsage::Default
        , 0
        , &s_ScreenQuadVertices[ 0 ]
        , D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER ), SD3D12ResourceDeferredDeleter() );
    if ( !m_ScreenQuadVerticesBuffer )
        return false;

    return true;
}

bool PostProcessingRenderer::OnFilmResolutionChange( uint32_t renderWidth, uint32_t renderHeight )
{
    return m_LuminanceRenderer.ResizeInputResolution( renderWidth, renderHeight );
}

void PostProcessingRenderer::ExecuteLuminanceCompute( const CScene& scene, const SRenderContext& renderContext )
{
    if ( m_IsAutoExposureEnabled && m_IsPostFXEnabled && renderContext.m_EnablePostFX )
    {
        m_LuminanceRenderer.Dispatch( scene, renderContext.m_CurrentResolutionWidth, renderContext.m_CurrentResolutionHeight );
    }
}

void PostProcessingRenderer::ExecutePostFX( const SRenderContext& renderContext, const CScene& scene )
{
    ID3D12GraphicsCommandList* commandList = D3D12Adapter::GetCommandList();

    SCOPED_RENDER_ANNOTATION( commandList, L"PostFX" );

    SConvolutionConstant constants;
    constants.m_ReciprocalPixelCount = 1.0f / ( renderContext.m_CurrentResolutionWidth * renderContext.m_CurrentResolutionHeight );
    constants.m_MaxWhiteSqr = m_LuminanceWhite * m_LuminanceWhite;
    constants.m_TexcoordScale = renderContext.m_CurrentResolutionRatio;
    constants.m_EV100 = m_CalculateEV100FromCamera ? CalculateEV100( scene.m_RelativeAperture, scene.m_ShutterTime, scene.m_ISO ) : m_ManualEV100;

    CD3D12ResourcePtr<GPUBuffer> constantBuffer( GPUBuffer::Create(
          sizeof( SConvolutionConstant )
        , 0
        , DXGI_FORMAT_UNKNOWN
        , EGPUBufferUsage::Dynamic
        , EGPUBufferBindFlag_ConstantBuffer
        , &constants ) );

    D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};
    vertexBufferView.BufferLocation = m_ScreenQuadVerticesBuffer->GetGPUVirtualAddress();
    vertexBufferView.SizeInBytes = sizeof( s_ScreenQuadVertices );
    vertexBufferView.StrideInBytes = sizeof( XMFLOAT4 );

    {
        D3D12_RESOURCE_BARRIER barriers[ 2 ];
        uint32_t barrierCount = 0;
        if ( m_LuminanceRenderer.GetLuminanceResultBuffer() )
        {
            barriers[ barrierCount++ ] = CD3DX12_RESOURCE_BARRIER::Transition( m_LuminanceRenderer.GetLuminanceResultBuffer()->GetBuffer(),
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE );
        }
        if ( scene.m_IsRenderResultTextureRead )
        {
            barriers[ barrierCount++ ] = CD3DX12_RESOURCE_BARRIER::Transition( scene.m_RenderResultTexture->GetTexture(),
                D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET );
        }
        if ( barrierCount )
        { 
            commandList->ResourceBarrier( barrierCount, barriers );
        }
    }
        
    commandList->SetGraphicsRootSignature( m_RootSignature.Get() );
    commandList->SetGraphicsRootConstantBufferView( 0, constantBuffer->GetGPUVirtualAddress() );

    SD3D12DescriptorHandle SRVs[] = { scene.m_FilmTexture->GetSRV(),
        m_LuminanceRenderer.GetLuminanceResultBuffer() ? m_LuminanceRenderer.GetLuminanceResultBuffer()->GetSRV() : D3D12Adapter::GetNullBufferSRV() };
    D3D12_GPU_DESCRIPTOR_HANDLE descriptorTable = s_DescriptorTableLayout.AllocateAndCopyToGPUDescriptorHeap( SRVs, (uint32_t)ARRAY_LENGTH( SRVs ), nullptr, 0 );
    commandList->SetGraphicsRootDescriptorTable( 1, descriptorTable );

    commandList->SetPipelineState( !m_IsPostFXEnabled || !renderContext.m_EnablePostFX ? m_PostFXDisabledPSO.Get() : ( m_IsAutoExposureEnabled ? m_PostFXAutoExposurePSO.Get() : m_PostFXPSO.Get() ) );
    commandList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
    commandList->IASetVertexBuffers( 0, 1, &vertexBufferView );
    commandList->DrawInstanced( 6, 1, 0, 0 );
}

void PostProcessingRenderer::ExecuteCopy( const CScene& scene )
{
    ID3D12GraphicsCommandList* commandList = D3D12Adapter::GetCommandList();

    SCOPED_RENDER_ANNOTATION( commandList, L"Copy" );
    
    SConvolutionConstant constants;
    constants.m_TexcoordScale = 1.0f;

    CD3D12ResourcePtr<GPUBuffer> constantBuffer( GPUBuffer::Create(
          sizeof( SConvolutionConstant )
        , 0
        , DXGI_FORMAT_UNKNOWN
        , EGPUBufferUsage::Dynamic
        , EGPUBufferBindFlag_ConstantBuffer
        , &constants ) );

    D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};
    vertexBufferView.BufferLocation = m_ScreenQuadVerticesBuffer->GetGPUVirtualAddress();
    vertexBufferView.SizeInBytes = sizeof( s_ScreenQuadVertices );
    vertexBufferView.StrideInBytes = sizeof( XMFLOAT4 );

    if ( !scene.m_IsRenderResultTextureRead )
    {
        D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition( scene.m_RenderResultTexture->GetTexture(),
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE );
        commandList->ResourceBarrier( 1, &barrier );
    }

    commandList->SetGraphicsRootSignature( m_RootSignature.Get() );
    commandList->SetGraphicsRootConstantBufferView( 0, constantBuffer->GetGPUVirtualAddress() );

    D3D12_GPU_DESCRIPTOR_HANDLE descriptorTable = s_DescriptorTableLayout.AllocateAndCopyToGPUDescriptorHeap( &scene.m_RenderResultTexture->GetSRV(), 1, nullptr, 0 );
    commandList->SetGraphicsRootDescriptorTable( 1, descriptorTable );

    commandList->SetPipelineState( m_CopyPSO.Get() );
    commandList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
    commandList->IASetVertexBuffers( 0, 1, &vertexBufferView );
    commandList->DrawInstanced( 6, 1, 0, 0 );
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
