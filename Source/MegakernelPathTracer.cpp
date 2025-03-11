#include "stdafx.h"
#include "MegakernelPathTracer.h"
#include "D3D12Adapter.h"
#include "D3D12Resource.h"
#include "D3D12DescriptorUtil.h"
#include "Logging.h"
#include "Shader.h"
#include "GPUBuffer.h"
#include "GPUTexture.h"
#include "Scene.h"
#include "RenderContext.h"
#include "BxDFTextures.h"
#include "MessageBox.h"
#include "ScopedRenderAnnotation.h"
#include "imgui/imgui.h"
#include "../Shaders/LightSharedDef.inc.hlsl"

using namespace DirectX;
using namespace D3D12Util;

#define CS_GROUP_SIZE_X 16
#define CS_GROUP_SIZE_Y 8

struct SRayTracingConstants
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
    uint32_t            padding[ 32 ]; // Padding the structure to 256B
};

struct SDebugConstants
{
    uint32_t m_IterationThreshold;
    uint32_t m_Padding[ 63 ]; // Padding the structure to 256B
};

static SD3D12DescriptorTableLayout s_DescriptorTableLayout = SD3D12DescriptorTableLayout( 15, 2 );

bool CMegakernelPathTracer::Create()
{
    // Static sampler
    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxAnisotropy = 1;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    CD3DX12_ROOT_PARAMETER1 rootParameters[ 4 ];
    rootParameters[ 0 ].InitAsConstantBufferView( 0 );
    rootParameters[ 1 ].InitAsConstantBufferView( 1 );
    rootParameters[ 2 ].InitAsConstantBufferView( 2 );
    SD3D12DescriptorTableRanges descriptorTableRanges;
    s_DescriptorTableLayout.InitRootParameter( &rootParameters[ 3 ], &descriptorTableRanges );

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc( 4, rootParameters, 1, &sampler );

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

    if ( FAILED( D3D12Adapter::GetDevice()->CreateRootSignature( 0, serializedRootSignature->GetBufferPointer(), serializedRootSignature->GetBufferSize(), IID_PPV_ARGS( m_RootSignature.GetAddressOf() ) ) ) )
    {
        return false;
    }

    m_RayTracingConstantsBuffer.reset( GPUBuffer::Create(
          sizeof( SRayTracingConstants )
        , 0
        , DXGI_FORMAT_UNKNOWN
        , EGPUBufferUsage::Default
        , EGPUBufferBindFlag_ConstantBuffer ) );
    if ( !m_RayTracingConstantsBuffer )
        return false;

    m_DebugConstantsBuffer.reset( GPUBuffer::Create(
          sizeof( SDebugConstants )
        , 0
        , DXGI_FORMAT_UNKNOWN
        , EGPUBufferUsage::Default
        , EGPUBufferBindFlag_ConstantBuffer ) );
    if ( !m_DebugConstantsBuffer )
        return false;

    return true;
}

void CMegakernelPathTracer::Destroy()
{
    m_RootSignature.Reset();
    m_PSO.reset();
    m_RayTracingConstantsBuffer.reset();
    m_DebugConstantsBuffer.reset();
}

void CMegakernelPathTracer::OnSceneLoaded()
{
    CompileAndCreateRayTracingKernel();
    m_FilmClearTrigger = true;
}

void CMegakernelPathTracer::Render( const SRenderContext& renderContext, const SBxDFTextures& BxDFTextures )
{
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
            m_Scene->m_Camera.GetTransformMatrix( &constants->cameraTransform );
            constants->filmDistance = m_Scene->CalculateFilmDistance();
            constants->filmSize = m_Scene->m_FilmSize;
            constants->lightCount = m_Scene->GetLightCount();
            constants->maxBounceCount = m_Scene->m_MaxBounceCount;
            constants->apertureRadius = m_Scene->CalculateApertureDiameter() * 0.5f;
            constants->focalDistance = m_Scene->m_FocalDistance;
            constants->apertureBaseAngle = m_Scene->m_ApertureRotation;
            constants->bladeCount = m_Scene->m_ApertureBladeCount;

            float halfBladeAngle = DirectX::XM_PI / m_Scene->m_ApertureBladeCount;
            constants->bladeVertexPos.x = cosf( halfBladeAngle ) * constants->apertureRadius;
            constants->bladeVertexPos.y = sinf( halfBladeAngle ) * constants->apertureRadius;

            constants->tileOffsetX = ( m_CurrentTileIndex % tileCountX ) * m_TileSize;
            constants->tileOffsetY = ( m_CurrentTileIndex / tileCountX ) * m_TileSize;

            constants->environmentLightIndex = m_Scene->m_EnvironmentLight ? (uint32_t)m_Scene->m_MeshLights.size() : LIGHT_INDEX_INVALID; // Environment light is right after the mesh lights.

            rayTracingConstantBufferUpload.Unmap();
        }
    }

    GPUBuffer::SUploadContext debugConstantBufferUpload;
    if ( m_OutputType > 0 )
    {
        if ( m_DebugConstantsBuffer->AllocateUploadContext( &debugConstantBufferUpload ) )
        {
            SDebugConstants* constants = (SDebugConstants*)debugConstantBufferUpload.Map();
            if ( constants )
            {
                constants->m_IterationThreshold = m_IterationThreshold;
                debugConstantBufferUpload.Unmap();
            }
        }
    }

    if ( rayTracingConstantBufferUpload.IsValid() )
    {
        rayTracingConstantBufferUpload.Upload();
    }
    if ( debugConstantBufferUpload.IsValid() )
    {
        debugConstantBufferUpload.Upload();
    }

    // Barriers
    {
        std::vector<D3D12_RESOURCE_BARRIER> barriers;
        barriers.reserve( 7 );

        barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( renderContext.m_RayTracingFrameConstantBuffer->GetBuffer(),
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER ) );
        barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_RayTracingConstantsBuffer->GetBuffer(),
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER ) );
        if ( m_Scene->m_IsSampleTexturesRead )
        {
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_Scene->m_SamplePositionTexture->GetTexture(),
                D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS ) );
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_Scene->m_SampleValueTexture->GetTexture(),
                D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS ) );
            m_Scene->m_IsSampleTexturesRead = false;
        }
        if ( !m_Scene->m_IsLightBufferRead )
        {
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_Scene->m_LightsBuffer->GetBuffer(),
                D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE ) );
            m_Scene->m_IsLightBufferRead = true;
        }
        if ( !m_Scene->m_IsMaterialBufferRead )
        {
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_Scene->m_MaterialsBuffer->GetBuffer(),
                D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE ) );
            m_Scene->m_IsMaterialBufferRead = true;
        }
        if ( m_OutputType > 0 )
        { 
            barriers.emplace_back( CD3DX12_RESOURCE_BARRIER::Transition( m_DebugConstantsBuffer->GetBuffer(),
                D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER ) );
        }

        commandList->ResourceBarrier( (uint32_t)barriers.size(), barriers.data() );
    }

    commandList->SetComputeRootSignature( m_RootSignature.Get() );

    commandList->SetComputeRootConstantBufferView( 0, m_RayTracingConstantsBuffer->GetGPUVirtualAddress() );
    commandList->SetComputeRootConstantBufferView( 1, renderContext.m_RayTracingFrameConstantBuffer->GetGPUVirtualAddress() );
    commandList->SetComputeRootConstantBufferView( 2, m_DebugConstantsBuffer->GetGPUVirtualAddress() );

    SD3D12DescriptorHandle environmentTextureSRV = D3D12Adapter::GetNullBufferSRV();
    if ( m_Scene->m_EnvironmentLight && m_Scene->m_EnvironmentLight->m_Texture )
    {
        environmentTextureSRV = m_Scene->m_EnvironmentLight->m_Texture->GetSRV();
    }
    SD3D12DescriptorHandle srcDescriptors[ 17 ] =
    {
          m_Scene->m_VerticesBuffer->GetSRV()
        , m_Scene->m_TrianglesBuffer->GetSRV()
        , m_Scene->m_LightsBuffer->GetSRV()
        , BxDFTextures.m_CookTorranceBRDF->GetSRV()
        , BxDFTextures.m_CookTorranceBRDFAverage->GetSRV()
        , BxDFTextures.m_CookTorranceBRDFDielectric->GetSRV()
        , BxDFTextures.m_CookTorranceBSDF->GetSRV()
        , BxDFTextures.m_CookTorranceBSDFAverage->GetSRV()
        , m_Scene->m_BVHNodesBuffer ? m_Scene->m_BVHNodesBuffer->GetSRV() : D3D12Adapter::GetNullBufferSRV()
        , m_Scene->m_InstanceTransformsBuffer->GetSRV( DXGI_FORMAT_UNKNOWN, sizeof( XMFLOAT4X3 ), 0, (uint32_t)m_Scene->m_InstanceTransforms.size() )
        , m_Scene->m_InstanceTransformsBuffer->GetSRV( DXGI_FORMAT_UNKNOWN, sizeof( XMFLOAT4X3 ), (uint32_t)m_Scene->m_InstanceTransforms.size(), (uint32_t)m_Scene->m_InstanceTransforms.size() )
        , m_Scene->m_MaterialIdsBuffer->GetSRV()
        , m_Scene->m_MaterialsBuffer->GetSRV()
        , m_Scene->m_InstanceLightIndicesBuffer->GetSRV()
        , environmentTextureSRV
        , m_Scene->m_SamplePositionTexture->GetUAV()
        , m_Scene->m_SampleValueTexture->GetUAV()
    };

    D3D12_GPU_DESCRIPTOR_HANDLE descriptorTable = s_DescriptorTableLayout.AllocateAndCopyToGPUDescriptorHeap( srcDescriptors, (uint32_t)ARRAY_LENGTH( srcDescriptors ) );
    commandList->SetComputeRootDescriptorTable( 3, descriptorTable );

    commandList->SetPipelineState( m_PSO.get() );
    
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

bool CMegakernelPathTracer::CompileAndCreateRayTracingKernel()
{
    std::vector<D3D_SHADER_MACRO> rayTracingShaderDefines;

    static const uint32_t s_MaxRadix10IntegerBufferLengh = 12;
    char buffer_TraversalStackSize[ s_MaxRadix10IntegerBufferLengh ];
    _itoa( m_Scene->m_BVHTraversalStackSize, buffer_TraversalStackSize, 10 );
    rayTracingShaderDefines.push_back( { "RT_BVH_TRAVERSAL_STACK_SIZE", buffer_TraversalStackSize } );

    char buffer_TraversalGroupSize[ s_MaxRadix10IntegerBufferLengh ];
    _itoa( CS_GROUP_SIZE_X * CS_GROUP_SIZE_Y, buffer_TraversalGroupSize, 10 );
    rayTracingShaderDefines.push_back( { "RT_BVH_TRAVERSAL_GROUP_SIZE", buffer_TraversalGroupSize } );

    rayTracingShaderDefines.push_back( { "GROUP_SIZE_X", DCRT_STRINGIFY_MACRO_VALUE( CS_GROUP_SIZE_X ) } );
    rayTracingShaderDefines.push_back( { "GROUP_SIZE_Y", DCRT_STRINGIFY_MACRO_VALUE( CS_GROUP_SIZE_Y ) } );

    if ( m_Scene->m_IsGGXVNDFSamplingEnabled )
    {
        rayTracingShaderDefines.push_back( { "GGX_SAMPLE_VNDF", "0" } );
    }
    if ( !m_Scene->m_TraverseBVHFrontToBack )
    {
        rayTracingShaderDefines.push_back( { "BVH_NO_FRONT_TO_BACK_TRAVERSAL", "0" } );
    }
    if ( m_Scene->m_IsLightVisible )
    {
        rayTracingShaderDefines.push_back( { "LIGHT_VISIBLE", "0" } );
    }
    if ( m_Scene->m_EnvironmentLight && m_Scene->m_EnvironmentLight->m_Texture )
    {
        rayTracingShaderDefines.push_back( { "HAS_ENV_TEXTURE", "0" } );
    }
    static const char* s_RayTracingOutputDefines[] = { "MEGAKERNEL", "OUTPUT_NORMAL", "OUTPUT_TANGENT", "OUTPUT_ALBEDO", "OUTPUT_NEGATIVE_NDOTV", "OUTPUT_BACKFACE", "OUTPUT_ITERATION_COUNT" };
    if ( s_RayTracingOutputDefines[ m_OutputType ] )
    {
        rayTracingShaderDefines.push_back( { s_RayTracingOutputDefines[ m_OutputType ], "0" } );
    }
    rayTracingShaderDefines.push_back( { NULL, NULL } );

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

    m_PSO.reset( PSO, SD3D12ComDeferredDeleter() );

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

void CMegakernelPathTracer::OnImGUI()
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
            CompileAndCreateRayTracingKernel();
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
