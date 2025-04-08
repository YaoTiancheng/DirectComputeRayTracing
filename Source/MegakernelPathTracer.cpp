#include "stdafx.h"
#include "MegakernelPathTracer.h"
#include "D3D12Adapter.h"
#include "D3D12Resource.h"
#include "D3D12DescriptorUtil.h"
#include "Logging.h"
#include "Shader.h"
#include "GPUBuffer.h"
#include "GPUTexture.h"
#include "DirectComputeRayTracing.h"
#include "RenderContext.h"
#include "MessageBox.h"
#include "ScopedRenderAnnotation.h"
#include "imgui/imgui.h"
#include "../Shaders/LightSharedDef.inc.hlsl"

using namespace DirectX;
using namespace D3D12Util;

#define CS_GROUP_SIZE_X 16
#define CS_GROUP_SIZE_Y 8

#define STRINGIFY( s ) STRINGIFY2( s )
#define STRINGIFY2( s ) L#s

struct alignas( 256 ) SRayTracingConstants
{
    DirectX::XMFLOAT4X4 cameraTransform;
    DirectX::XMFLOAT2   filmSize;
    uint32_t            resolutionX;
    uint32_t            resolutionY;
    uint32_t            maxBounceCount;
    uint32_t            lightCount;
    float               apertureRadius;
    float               focalDistance;
    float               filmDistance;
    DirectX::XMFLOAT2   bladeVertexPos;
    uint32_t            bladeCount;
    float               apertureBaseAngle;
    uint32_t            tileOffsetX;
    uint32_t            tileOffsetY;
    uint32_t            environmentLightIndex;
    uint32_t            frameSeed;
};

static SD3D12DescriptorTableLayout s_DescriptorTableLayout = SD3D12DescriptorTableLayout( 15, 2 );

bool CMegakernelPathTracer::Create()
{
    // Static samplers
    D3D12_STATIC_SAMPLER_DESC samplers[ 2 ] = {};
    for ( D3D12_STATIC_SAMPLER_DESC& sampler : samplers )
    {
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.MaxAnisotropy = 1;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    // UVClamp
    samplers[ 0 ].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[ 0 ].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[ 0 ].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[ 0 ].ShaderRegister = 0U;
    // UVWrap
    samplers[ 1 ].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[ 1 ].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[ 1 ].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[ 1 ].ShaderRegister = 1U;

    CD3DX12_ROOT_PARAMETER1 rootParameters[ 3 ];
    rootParameters[ 0 ].InitAsConstantBufferView( 0 );
    rootParameters[ 1 ].InitAsConstants( 1, 1 );
    SD3D12DescriptorTableRanges descriptorTableRanges;
    s_DescriptorTableLayout.InitRootParameter( &rootParameters[ 2 ], &descriptorTableRanges );

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc( 3, rootParameters, (uint32_t)ARRAY_LENGTH( samplers ), samplers );

    ComPtr<ID3DBlob> serializedRootSignature;
    ComPtr<ID3DBlob> error;
    HRESULT hr = D3D12SerializeVersionedRootSignature( &rootSignatureDesc, serializedRootSignature.GetAddressOf(), error.GetAddressOf() ); 
    if ( error )
    { 
        LOG_STRING_FORMAT( "Create mega-kernel path tracing root signature with error: %s\n", (const char*)error->GetBufferPointer() );
    }
    if ( FAILED( hr ) )
    {
        return false;
    }

    ID3D12RootSignature* rootSignature = nullptr;
    if ( FAILED( D3D12Adapter::GetDevice()->CreateRootSignature( 0, serializedRootSignature->GetBufferPointer(), serializedRootSignature->GetBufferSize(), IID_PPV_ARGS( &rootSignature ) ) ) )
    {
        return false;
    }
    m_RootSignature.Reset( rootSignature );

    m_RayTracingConstantsBuffer.Reset( GPUBuffer::Create(
          sizeof( SRayTracingConstants )
        , 0
        , DXGI_FORMAT_UNKNOWN
        , EGPUBufferUsage::Default
        , EGPUBufferBindFlag_ConstantBuffer ) );
    if ( !m_RayTracingConstantsBuffer )
        return false;

    return true;
}

void CMegakernelPathTracer::Destroy()
{
    m_RootSignature.Reset();
    m_PSO.Reset();
    m_RayTracingConstantsBuffer.Reset();
}

void CMegakernelPathTracer::OnSceneLoaded( SRenderer* renderer )
{
    CompileAndCreateRayTracingKernel( renderer );
    m_FilmClearTrigger = true;
}

void CMegakernelPathTracer::Render( SRenderer* renderer, const SRenderContext& renderContext )
{
    CScene* scene = &renderer->m_Scene;
    SBxDFTextures* BxDFTextures = &renderer->m_BxDFTextures;

    ID3D12GraphicsCommandList* commandList = D3D12Adapter::GetCommandList();

    SCOPED_RENDER_ANNOTATION( commandList, L"Dispatch rays" );

    uint32_t tileCountX = (uint32_t)std::ceilf( float( renderContext.m_CurrentResolutionWidth ) / float( m_TileSize ) );
    uint32_t tileCountY = (uint32_t)std::ceilf( float( renderContext.m_CurrentResolutionHeight ) / float( m_TileSize ) );

    GPUBuffer::SUploadContext rayTracingConstantBufferUpload;
    if ( m_RayTracingConstantsBuffer->AllocateUploadContext( &rayTracingConstantBufferUpload ) )
    {
        SRayTracingConstants* constants = (SRayTracingConstants*)rayTracingConstantBufferUpload.Map();
        if ( constants )
        {
            constants->resolutionX = renderContext.m_CurrentResolutionWidth;
            constants->resolutionY = renderContext.m_CurrentResolutionHeight;
            scene->m_Camera.GetTransformMatrix( &constants->cameraTransform );
            constants->filmDistance = scene->CalculateFilmDistance();
            constants->filmSize = scene->m_FilmSize;
            constants->lightCount = scene->GetLightCount();
            constants->maxBounceCount = scene->m_MaxBounceCount;
            constants->apertureRadius = scene->CalculateApertureDiameter() * 0.5f;
            constants->focalDistance = scene->m_FocalDistance;
            constants->apertureBaseAngle = scene->m_ApertureRotation;
            constants->bladeCount = scene->m_ApertureBladeCount;

            float halfBladeAngle = DirectX::XM_PI / scene->m_ApertureBladeCount;
            constants->bladeVertexPos.x = cosf( halfBladeAngle ) * constants->apertureRadius;
            constants->bladeVertexPos.y = sinf( halfBladeAngle ) * constants->apertureRadius;

            constants->tileOffsetX = ( m_CurrentTileIndex % tileCountX ) * m_TileSize;
            constants->tileOffsetY = ( m_CurrentTileIndex / tileCountX ) * m_TileSize;

            constants->environmentLightIndex = scene->m_EnvironmentLight ? (uint32_t)scene->m_MeshLights.size() : LIGHT_INDEX_INVALID; // Environment light is right after the mesh lights.

            constants->frameSeed = renderer->m_FrameSeed;

            rayTracingConstantBufferUpload.Unmap();
            rayTracingConstantBufferUpload.Upload();
        }
    }

    // Barriers
    {
        std::vector<D3D12_RESOURCE_BARRIER> barriers;
        barriers.reserve( 5 );

        barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_RayTracingConstantsBuffer->GetBuffer(),
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER ) );
        if ( scene->m_IsSampleTexturesRead )
        {
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( scene->m_SamplePositionTexture->GetTexture(),
                D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS ) );
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( scene->m_SampleValueTexture->GetTexture(),
                D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS ) );
            scene->m_IsSampleTexturesRead = false;
        }
        if ( !scene->m_IsLightBufferRead )
        {
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( scene->m_LightsBuffer->GetBuffer(),
                D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE ) );
            scene->m_IsLightBufferRead = true;
        }
        if ( !scene->m_IsMaterialBufferRead )
        {
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( scene->m_MaterialsBuffer->GetBuffer(),
                D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE ) );
            scene->m_IsMaterialBufferRead = true;
        }

        commandList->ResourceBarrier( (uint32_t)barriers.size(), barriers.data() );
    }

    commandList->SetComputeRootSignature( m_RootSignature.Get() );

    commandList->SetComputeRootConstantBufferView( 0, m_RayTracingConstantsBuffer->GetGPUVirtualAddress() );
    if ( m_OutputType > 0 )
    { 
        uint32_t iterationThreshold = m_IterationThreshold;
        commandList->SetComputeRoot32BitConstant( 1, iterationThreshold, 0 );
    }

    SD3D12DescriptorHandle environmentTextureSRV = D3D12Adapter::GetNullBufferSRV();
    if ( scene->m_EnvironmentLight && scene->m_EnvironmentLight->m_Texture )
    {
        environmentTextureSRV = scene->m_EnvironmentLight->m_Texture->GetSRV();
    }
    std::vector<SD3D12DescriptorHandle> srcDescriptors;
    const uint32_t fixedDescriptorCount = s_DescriptorTableLayout.m_SRVCount + s_DescriptorTableLayout.m_UAVCount;
    srcDescriptors.reserve( fixedDescriptorCount + scene->m_GPUTextures.size() );
    srcDescriptors = 
    {
          scene->m_VerticesBuffer->GetSRV()
        , scene->m_TrianglesBuffer->GetSRV()
        , scene->m_LightsBuffer->GetSRV()
        , BxDFTextures->m_CookTorranceBRDF->GetSRV()
        , BxDFTextures->m_CookTorranceBRDFAverage->GetSRV()
        , BxDFTextures->m_CookTorranceBRDFDielectric->GetSRV()
        , BxDFTextures->m_CookTorranceBSDF->GetSRV()
        , BxDFTextures->m_CookTorranceBSDFAverage->GetSRV()
        , scene->m_BVHNodesBuffer ? scene->m_BVHNodesBuffer->GetSRV() : D3D12Adapter::GetNullBufferSRV()
        , scene->m_InstanceTransformsBuffer->GetSRV( DXGI_FORMAT_UNKNOWN, sizeof( XMFLOAT4X3 ), 0, (uint32_t)scene->m_InstanceTransforms.size() )
        , scene->m_InstanceTransformsBuffer->GetSRV( DXGI_FORMAT_UNKNOWN, sizeof( XMFLOAT4X3 ), (uint32_t)scene->m_InstanceTransforms.size(), (uint32_t)scene->m_InstanceTransforms.size() )
        , scene->m_MaterialIdsBuffer->GetSRV()
        , scene->m_MaterialsBuffer->GetSRV()
        , scene->m_InstanceLightIndicesBuffer->GetSRV()
        , environmentTextureSRV
        , scene->m_SamplePositionTexture->GetUAV()
        , scene->m_SampleValueTexture->GetUAV()
    };
    // Copy scene texture descriptors
    srcDescriptors.resize( srcDescriptors.size() + scene->m_GPUTextures.size() );
    scene->CopyTextureDescriptors( srcDescriptors.data() + fixedDescriptorCount );

    D3D12_GPU_DESCRIPTOR_HANDLE descriptorTable = D3D12Util::AllocateAndCopyToDescriptorTable( srcDescriptors.data(), (uint32_t)srcDescriptors.size() );
    commandList->SetComputeRootDescriptorTable( 2, descriptorTable );

    commandList->SetPipelineState( m_PSO.Get() );
    
    uint32_t dispatchThreadWidth = renderContext.m_IsSmallResolutionEnabled ? renderContext.m_CurrentResolutionWidth : m_TileSize;
    uint32_t dispatchThreadHeight = renderContext.m_IsSmallResolutionEnabled ? renderContext.m_CurrentResolutionHeight : m_TileSize;
    uint32_t dispatchSizeX = (uint32_t)ceil( dispatchThreadWidth / (float)CS_GROUP_SIZE_X );
    uint32_t dispatchSizeY = (uint32_t)ceil( dispatchThreadHeight / (float)CS_GROUP_SIZE_Y );
    uint32_t dispatchSizeZ = 1;

    commandList->Dispatch( dispatchSizeX, dispatchSizeY, dispatchSizeZ );

    uint32_t tileCount = tileCountX * tileCountY;
    m_CurrentTileIndex = ( m_CurrentTileIndex + 1 ) % tileCount;
}

void CMegakernelPathTracer::ResetImage()
{
    ResetTileIndex();
}

bool CMegakernelPathTracer::IsImageComplete()
{
    return AreAllTilesRendered();
}

bool CMegakernelPathTracer::CompileAndCreateRayTracingKernel( SRenderer* renderer )
{
    CScene* scene = &renderer->m_Scene;

    std::vector<DxcDefine> rayTracingShaderDefines;

    static const uint32_t s_MaxRadix10IntegerBufferLengh = 12;
    wchar_t buffer_TraversalStackSize[ s_MaxRadix10IntegerBufferLengh ];
    _itow( scene->m_BVHTraversalStackSize, buffer_TraversalStackSize, 10 );
    rayTracingShaderDefines.push_back( { L"RT_BVH_TRAVERSAL_STACK_SIZE", buffer_TraversalStackSize } );

    wchar_t buffer_TraversalGroupSize[ s_MaxRadix10IntegerBufferLengh ];
    _itow( CS_GROUP_SIZE_X * CS_GROUP_SIZE_Y, buffer_TraversalGroupSize, 10 );
    rayTracingShaderDefines.push_back( { L"RT_BVH_TRAVERSAL_GROUP_SIZE", buffer_TraversalGroupSize } );

    rayTracingShaderDefines.push_back( { L"GROUP_SIZE_X", STRINGIFY( CS_GROUP_SIZE_X ) } );
    rayTracingShaderDefines.push_back( { L"GROUP_SIZE_Y", STRINGIFY( CS_GROUP_SIZE_Y ) } );

    if ( scene->m_IsGGXVNDFSamplingEnabled )
    {
        rayTracingShaderDefines.push_back( { L"GGX_SAMPLE_VNDF", L"0" } );
    }
    if ( !scene->m_TraverseBVHFrontToBack )
    {
        rayTracingShaderDefines.push_back( { L"BVH_NO_FRONT_TO_BACK_TRAVERSAL", L"0" } );
    }
    if ( scene->m_IsLightVisible )
    {
        rayTracingShaderDefines.push_back( { L"LIGHT_VISIBLE", L"0" } );
    }
    if ( scene->m_EnvironmentLight && scene->m_EnvironmentLight->m_Texture )
    {
        rayTracingShaderDefines.push_back( { L"HAS_ENV_TEXTURE", L"0" } );
    }
    static const wchar_t* s_RayTracingOutputDefines[] = { L"MEGAKERNEL", L"OUTPUT_NORMAL", L"OUTPUT_TANGENT", L"OUTPUT_ALBEDO", L"OUTPUT_NEGATIVE_NDOTV", L"OUTPUT_BACKFACE", L"OUTPUT_ITERATION_COUNT" };
    if ( s_RayTracingOutputDefines[ m_OutputType ] )
    {
        rayTracingShaderDefines.push_back( { s_RayTracingOutputDefines[ m_OutputType ], L"0" } );
    }

    ComputeShaderPtr rayTracingShader( ComputeShader::CreateFromFile( L"Shaders\\MegakernelPathTracing.hlsl", rayTracingShaderDefines ) );
    if ( !rayTracingShader )
    {
        CMessagebox::GetSingleton().Append( "Failed to compile ray tracing shader.\n" );
        return false;
    }

    // Create PSO
    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_RootSignature.Get();
    psoDesc.CS = rayTracingShader->GetShaderBytecode();
    psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    ID3D12PipelineState* PSO = nullptr;
    if ( FAILED( D3D12Adapter::GetDevice()->CreateComputePipelineState( &psoDesc, IID_PPV_ARGS( &PSO ) ) ) )
    {
        return false;
    }

    m_PSO.Reset( PSO );

    return true;
}

void CMegakernelPathTracer::ResetTileIndex()
{
    m_CurrentTileIndex = 0;
}

bool CMegakernelPathTracer::AreAllTilesRendered() const
{
    return m_CurrentTileIndex == 0;
}

void CMegakernelPathTracer::OnImGUI( SRenderer* renderer )
{
    if ( ImGui::CollapsingHeader( "Megakernel Path Tracer" ) )
    {
        ImGui::InputInt( "Render Tile Size", (int*)&m_TileSize, 16, 32 );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
        {
            m_TileSize = std::max( (uint32_t)16, m_TileSize );
            m_FilmClearTrigger = true;
        }

        static const char* s_OutputNames[] = { "Path Tracing", "Shading Normal", "Shading Tangent", "Albedo", "Negative NdotV", "Backface", "Iteration Count"};
        if ( ImGui::Combo( "Output", (int*)&m_OutputType, s_OutputNames, IM_ARRAYSIZE( s_OutputNames ) ) )
        {
            CompileAndCreateRayTracingKernel( renderer );
            m_FilmClearTrigger = true;
        }
        
        if ( m_OutputType == 6 )
        {
            if ( ImGui::DragInt( "Iteration Threshold", (int*)&m_IterationThreshold, 1.f, 1, 10000, "%d", ImGuiSliderFlags_AlwaysClamp ) )
            {
                m_FilmClearTrigger = true;
            }
        }
    }
}

bool CMegakernelPathTracer::AcquireFilmClearTrigger()
{
    bool value = m_FilmClearTrigger;
    m_FilmClearTrigger = false;
    return value;
}
