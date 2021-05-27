#include "stdafx.h"
#include "DirectComputeRayTracing.h"
#include "D3D11RenderSystem.h"
#include "CommandLineArgs.h"
#include "Mesh.h"
#include "GPUTexture.h"
#include "GPUBuffer.h"
#include "Shader.h"
#include "Logging.h"
#include "StringConversion.h"
#include "Camera.h"
#include "ComputeJob.h"
#include "PostProcessingRenderer.h"
#include "SceneLuminanceRenderer.h"
#include "Timers.h"
#include "Rectangle.h"
#include "BxDFTexturesBuilder.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_win32.h"
#include "../Shaders/Light.inc.hlsl"
#include "../Shaders/Material.inc.hlsl"
#include "../Shaders/SumLuminanceDef.inc.hlsl"

using namespace DirectX;

static const uint32_t s_MaxRayBounce = 20;
static const uint32_t s_MaxLightsCount = 64;
static const int s_RayTracingKernelCount = 6;

struct RayTracingConstants
{
    DirectX::XMFLOAT4X4             cameraTransform;
    DirectX::XMFLOAT2               filmSize;
    uint32_t                        resolutionX;
    uint32_t                        resolutionY;
    DirectX::XMFLOAT4               background;
    uint32_t                        maxBounceCount;
    uint32_t                        primitiveCount;
    uint32_t                        lightCount;
    float                           filmDistance;
};

enum class ELightType
{
    Point     = 0,
    Rectangle = 1,
};

struct SLightSetting
{
    XMFLOAT3                        position;
    XMFLOAT3                        rotation;
    XMFLOAT3                        color;
    XMFLOAT2                        size;
    ELightType                      lightType;
};

struct SSceneObjectSelection
{
    void SelectLight( int index )
    {
        DeselectAll();
        m_LightSelectionIndex = index;
    }

    void SelectMaterial( int index )
    {
        DeselectAll();
        m_MaterialSelectionIndex = index;
    }

    void SelectCamera()
    {
        DeselectAll();
        m_IsCameraSelected = true;
    }

    void DeselectAll()
    {
        m_LightSelectionIndex = -1;
        m_MaterialSelectionIndex = -1;
        m_IsCameraSelected = false;
    }

    int                             m_LightSelectionIndex = -1;
    int                             m_MaterialSelectionIndex = -1;
    bool                            m_IsCameraSelected = false;
};

struct SRenderer
{
    explicit SRenderer( HWND hWnd )
        : m_hWnd( hWnd )
    {
    }

    bool OnWndMessage( UINT message, WPARAM wParam, LPARAM lParam );

    bool Init();

    bool ResetScene( const char* filepath );

    void DispatchRayTracing();

    void RenderOneFrame();

    bool UpdateResources();

    void ClearFilmTexture();

    void UpdateRayTracingJob();

    void UpdateRenderViewport( uint32_t backbufferWidth, uint32_t backbufferHeight );

    void ResizeBackbuffer( uint32_t backbufferWidth, uint32_t backbufferHeight );

    void CreateEnvironmentTextureFromCurrentFilepath();

    bool CompileAndCreateRayTracingKernels( uint32_t traversalStackSize, bool noBVHAccel, bool multipleImportanceSampling );

    void OnImGUI();

    HWND                                m_hWnd;

    Camera                              m_Camera;
    std::vector<SLightSetting>          m_LightSettings;
    std::vector<Material>               m_Materials;
    std::vector<std::string>            m_MaterialNames;

    RayTracingConstants                 m_RayTracingConstants;

    uint32_t                            m_FrameSeed = 0;

    bool                                m_IsFilmDirty = true;
    bool                                m_IsConstantBufferDirty = true;
    bool                                m_IsLightBufferDirty = false;
    bool                                m_IsMaterialBufferDirty = false;
    bool                                m_IsRayTracingJobDirty = true;

    template <typename T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;
    ComPtr<ID3D11SamplerState>          m_UVClampSamplerState;

    ComputeShaderPtr                    m_RayTracingShader[ s_RayTracingKernelCount ];

    GPUTexturePtr                       m_FilmTexture;
    GPUTexturePtr                       m_RenderResultTexture;
    GPUTexturePtr                       m_sRGBBackbuffer;
    GPUTexturePtr                       m_LinearBackbuffer;
    GPUTexturePtr                       m_CookTorranceCompETexture;
    GPUTexturePtr                       m_CookTorranceCompEAvgTexture;
    GPUTexturePtr                       m_CookTorranceCompInvCDFTexture;
    GPUTexturePtr                       m_CookTorranceCompPdfScaleTexture;
    GPUTexturePtr                       m_CookTorranceCompEFresnelTexture;
    GPUTexturePtr                       m_CookTorranceBSDFEFresnelTexture;
    GPUTexturePtr                       m_EnvironmentTexture;

    GPUBufferPtr                        m_RayTracingConstantsBuffer;
    GPUBufferPtr                        m_RayTracingFrameConstantBuffer;
    GPUBufferPtr                        m_VerticesBuffer;
    GPUBufferPtr                        m_TrianglesBuffer;
    GPUBufferPtr                        m_BVHNodesBuffer;
    GPUBufferPtr                        m_LightsBuffer;
    GPUBufferPtr                        m_MaterialIdsBuffer;
    GPUBufferPtr                        m_MaterialsBuffer;

    PostProcessingRenderer              m_PostProcessing;
    SceneLuminanceRenderer              m_SceneLuminance;

    ComputeJob                          m_RayTracingJob;

    enum class EFrameSeedType { FrameIndex = 0, SampleCount = 1, Fixed = 2, _Count = 3 };
    EFrameSeedType                      m_FrameSeedType = EFrameSeedType::FrameIndex;

    FrameTimer                          m_FrameTimer;

    SRectangle                          m_RenderViewport;

    std::string                         m_EnvironmentImageFilepath;

    bool                                m_HasValidScene;
    bool                                m_IsMultipleImportanceSamplingEnabled;

    int                                 m_RayTracingKernelIndex = 0;
    SSceneObjectSelection               m_SceneObjectSelection;
    bool                                m_ShowUI = true;
};

SRenderer* s_Renderer = nullptr;

struct RayTracingFrameConstants
{
    uint32_t frameSeed;
    uint32_t padding[ 3 ];
};

static bool InitImGui( HWND hWnd )
{
    IMGUI_CHECKVERSION();

    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    if ( !ImGui_ImplWin32_Init( hWnd ) )
    {
        ImGui::DestroyContext();
        return false;
    }

    if ( !ImGui_ImplDX11_Init( GetDevice(), GetDeviceContext() ) )
    {
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    return true;
}

static void ShutDownImGui()
{
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

static void ImGUINewFrame()
{
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

CDirectComputeRayTracing::CDirectComputeRayTracing( HWND hWnd )
{
    s_Renderer = new SRenderer( hWnd );
}

CDirectComputeRayTracing::~CDirectComputeRayTracing()
{
    delete s_Renderer;
    ShutDownImGui();
    FiniRenderSystem();
}

bool CDirectComputeRayTracing::Init()
{
    if ( !InitRenderSystem( s_Renderer->m_hWnd ) )
        return false;

    if ( !InitImGui( s_Renderer->m_hWnd ) )
        return false;

    return s_Renderer->Init();
}

void CDirectComputeRayTracing::RenderOneFrame()
{
    s_Renderer->RenderOneFrame();
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam );

bool CDirectComputeRayTracing::OnWndMessage( UINT message, WPARAM wParam, LPARAM lParam )
{
    if ( ImGui_ImplWin32_WndProcHandler( s_Renderer->m_hWnd, message, wParam, lParam ) )
        return true;

    if ( message == WM_SIZE )
    {
        UINT width = LOWORD( lParam );
        UINT height = HIWORD( lParam );
        s_Renderer->ResizeBackbuffer( width, height );
        s_Renderer->UpdateRenderViewport( width, height );
    }

    return s_Renderer->OnWndMessage( message, wParam, lParam );
}

bool SRenderer::OnWndMessage( UINT message, WPARAM wParam, LPARAM lParam )
{
    return m_Camera.OnWndMessage( message, wParam, lParam );
}

bool SRenderer::Init()
{
    m_EnvironmentImageFilepath = StringConversion::UTF16WStringToUTF8String( CommandLineArgs::Singleton()->GetEnvironmentTextureFilename() );

    uint32_t resolutionWidth = CommandLineArgs::Singleton()->ResolutionX();
    uint32_t resolutionHeight = CommandLineArgs::Singleton()->ResolutionY();

    ID3D11Device* device = GetDevice();

    m_FilmTexture.reset( GPUTexture::Create(
          resolutionWidth
        , resolutionHeight
        , DXGI_FORMAT_R32G32B32A32_FLOAT
        , GPUResourceCreationFlags_HasUAV | GPUResourceCreationFlags_IsRenderTarget ) );
    if ( !m_FilmTexture )
        return false;

    m_CookTorranceCompETexture.reset( BxDFTexturesBuilder::CreateCoorkTorranceBRDFEnergyTexture() );
    if ( !m_CookTorranceCompETexture )
        return false;

    m_CookTorranceCompEAvgTexture.reset( BxDFTexturesBuilder::CreateCookTorranceBRDFAverageEnergyTexture() );
    if ( !m_CookTorranceCompEAvgTexture )
        return false;

    m_CookTorranceCompInvCDFTexture.reset( BxDFTexturesBuilder::CreateCookTorranceMultiscatteringBRDFInvCDFTexture() );
    if ( !m_CookTorranceCompInvCDFTexture )
        return false;

    m_CookTorranceCompPdfScaleTexture.reset( BxDFTexturesBuilder::CreateCookTorranceMultiscatteringBRDFPDFScaleTexture() );
    if ( !m_CookTorranceCompPdfScaleTexture )
        return false;

    m_CookTorranceCompEFresnelTexture.reset( BxDFTexturesBuilder::CreateCoorkTorranceBRDFEnergyFresnelDielectricTexture() );
    if ( !m_CookTorranceCompEFresnelTexture )
        return false;

    m_CookTorranceBSDFEFresnelTexture.reset( BxDFTexturesBuilder::CreateCookTorranceBSDFEnergyFresnelDielectricTexture() );
    if ( !m_CookTorranceBSDFEFresnelTexture )
        return false;

    CreateEnvironmentTextureFromCurrentFilepath();
    if ( !m_EnvironmentTexture )
        return false;

    m_RayTracingConstantsBuffer.reset( GPUBuffer::Create(
          sizeof( RayTracingConstants )
        , 0
        , GPUResourceCreationFlags_CPUWriteable | GPUResourceCreationFlags_IsConstantBuffer ) );
    if ( !m_RayTracingConstantsBuffer )
        return false;

    m_RayTracingFrameConstantBuffer.reset( GPUBuffer::Create(
          sizeof( RayTracingFrameConstants )
        , 0
        , GPUResourceCreationFlags_CPUWriteable | GPUResourceCreationFlags_IsConstantBuffer ) );
    if ( !m_RayTracingFrameConstantBuffer )
        return false;

    m_RenderResultTexture.reset( GPUTexture::Create( resolutionWidth, resolutionHeight, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, GPUResourceCreationFlags_IsRenderTarget ) );
    if ( !m_RenderResultTexture )
        return false;

    m_sRGBBackbuffer.reset( GPUTexture::CreateFromSwapChain( DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ) );
    if ( !m_sRGBBackbuffer)
        return false;

    m_LinearBackbuffer.reset( GPUTexture::CreateFromSwapChain() );
    if ( !m_LinearBackbuffer )
        return false;

    D3D11_SAMPLER_DESC samplerDesc;
    ZeroMemory( &samplerDesc, sizeof( D3D11_SAMPLER_DESC ) );
    samplerDesc.Filter = D3D11_FILTER_MAXIMUM_MIN_MAG_MIP_POINT;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MaxAnisotropy = 1;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    HRESULT hr = device->CreateSamplerState( &samplerDesc, &m_UVClampSamplerState );
    if ( FAILED( hr ) )
        return false;

    m_RayTracingConstants.resolutionX = resolutionWidth;
    m_RayTracingConstants.resolutionY = resolutionHeight;

    if ( !m_SceneLuminance.Init( resolutionWidth, resolutionHeight, m_FilmTexture ) )
        return false;

    if ( !m_PostProcessing.Init( resolutionWidth, resolutionHeight, m_FilmTexture, m_RenderResultTexture, m_SceneLuminance.GetLuminanceResultBuffer() ) )
        return false;

    UpdateRenderViewport( resolutionWidth, resolutionHeight );

    m_HasValidScene = ResetScene( CommandLineArgs::Singleton()->GetFilename().c_str() );

    return true;
}

bool SRenderer::ResetScene( const char* filePath )
{
    m_IsFilmDirty = true; // Clear film in case scene reset failed and ray tracing being disabled.

    const CommandLineArgs* commandLineArgs = CommandLineArgs::Singleton();

    Mesh mesh;
    {
        char filePathNoExtension[ MAX_PATH ];
        char fileDir[ MAX_PATH ];
        strcpy( filePathNoExtension, filePath );
        PathRemoveExtensionA( filePathNoExtension );
        const char* fileName = PathFindFileNameA( filePathNoExtension );
        strcpy( fileDir, filePath );
        PathRemoveFileSpecA( fileDir );

        char BVHFilePath[ MAX_PATH ];
        const char* MTLSearchPath = fileDir;
        sprintf_s( BVHFilePath, MAX_PATH, "%s\\%s.xml", fileDir, fileName );
        bool buildBVH = !commandLineArgs->GetNoBVHAccel();

        LOG_STRING_FORMAT( "Loading mesh from: %s, MTL search path at: %s, BVH file path at: %s\n", filePath, MTLSearchPath, BVHFilePath );

        if ( !mesh.LoadFromOBJFile( filePath, MTLSearchPath, buildBVH, BVHFilePath ) )
            return false;

        LOG_STRING_FORMAT( "Mesh loaded. Triangle count: %d, vertex count: %d, material count: %d\n", mesh.GetTriangleCount(), mesh.GetVertexCount(), mesh.GetMaterials().size() );
    }

    if ( !commandLineArgs->GetNoBVHAccel() )
    {
        uint32_t BVHMaxDepth = mesh.GetBVHMaxDepth();
        uint32_t BVHMaxStackSize = mesh.GetBVHMaxStackSize();
        LOG_STRING_FORMAT( "BVH created from mesh. Node count:%d, max depth:%d, max stack size:%d\n", mesh.GetBVHNodeCount(), BVHMaxDepth, BVHMaxStackSize );
    }

    if ( !CompileAndCreateRayTracingKernels( mesh.GetBVHMaxStackSize(), commandLineArgs->GetNoBVHAccel(), commandLineArgs->IsMultipleImportanceSamplingEnabled() ) )
        return false;

    // Update after shader created successfully so the UI display match the real shader option state.
    m_IsMultipleImportanceSamplingEnabled = commandLineArgs->IsMultipleImportanceSamplingEnabled();

    m_Materials = mesh.GetMaterials();
    m_MaterialNames = mesh.GetMaterialNames();

    m_VerticesBuffer.reset( GPUBuffer::Create(
          sizeof( Vertex ) * mesh.GetVertexCount()
        , sizeof( Vertex )
        , GPUResourceCreationFlags_IsImmutable | GPUResourceCreationFlags_IsStructureBuffer
        , mesh.GetVertices() ) );
    if ( !m_VerticesBuffer )
        return false;

    m_TrianglesBuffer.reset( GPUBuffer::Create(
          sizeof( uint32_t ) * mesh.GetIndexCount()
        , sizeof( uint32_t )
        , GPUResourceCreationFlags_IsImmutable | GPUResourceCreationFlags_IsStructureBuffer
        , mesh.GetIndices() ) );
    if ( !m_TrianglesBuffer )
        return false;

    m_MaterialIdsBuffer.reset( GPUBuffer::Create(
          sizeof( uint32_t ) * mesh.GetTriangleCount()
        , sizeof( uint32_t )
        , GPUResourceCreationFlags_IsImmutable | GPUResourceCreationFlags_IsStructureBuffer
        , mesh.GetMaterialIds() ) );
    if ( !m_MaterialIdsBuffer )
        return false;

    m_MaterialsBuffer.reset( GPUBuffer::Create(
          uint32_t( sizeof( Material ) * m_Materials.size() )
        , sizeof( Material )
        , GPUResourceCreationFlags_CPUWriteable | GPUResourceCreationFlags_IsStructureBuffer
        , m_Materials.data() ) );
    if ( !m_MaterialsBuffer )
        return false;

    if ( !commandLineArgs->GetNoBVHAccel() )
    {
        m_BVHNodesBuffer.reset( GPUBuffer::Create(
              sizeof( BVHNode ) * mesh.GetBVHNodeCount()
            , sizeof( BVHNode )
            , GPUResourceCreationFlags_IsImmutable | GPUResourceCreationFlags_IsStructureBuffer
            , mesh.GetBVHNodes() ) );
        if ( !m_BVHNodesBuffer )
            return false;
    }

    m_LightSettings.clear();
    m_LightSettings.reserve( s_MaxLightsCount );

    m_LightsBuffer.reset( GPUBuffer::Create(
          sizeof( SLight ) * s_MaxLightsCount
        , sizeof( SLight )
        , GPUResourceCreationFlags_CPUWriteable | GPUResourceCreationFlags_IsStructureBuffer
        , nullptr ) );
    if ( !m_LightsBuffer )
        return false;

    m_RayTracingConstants.maxBounceCount = 2;
    m_RayTracingConstants.primitiveCount = mesh.GetTriangleCount();
    m_RayTracingConstants.lightCount = (uint32_t)m_LightSettings.size();
    m_RayTracingConstants.filmSize = XMFLOAT2( 0.05333f, 0.03f );
    m_RayTracingConstants.filmDistance = 0.04f;
    m_RayTracingConstants.background = { 1.0f, 1.0f, 1.0f, 0.f };

    m_IsConstantBufferDirty = true;
    m_IsMaterialBufferDirty = true;
    m_IsLightBufferDirty = true;
    m_IsRayTracingJobDirty = true;
    m_Camera.SetDirty();

    m_SceneObjectSelection.DeselectAll();

    return true;
}

void SRenderer::DispatchRayTracing()
{
    m_Camera.Update( m_FrameTimer.GetCurrentFrameDeltaTime() );

    if ( m_Camera.IsDirty() )
    {
        m_Camera.GetTransformMatrixAndClearDirty( &m_RayTracingConstants.cameraTransform );
        m_IsConstantBufferDirty = true;
    }

    m_IsFilmDirty = m_IsFilmDirty || m_IsConstantBufferDirty || m_IsLightBufferDirty || m_IsMaterialBufferDirty || m_IsRayTracingJobDirty;

    if ( m_IsFilmDirty )
    {
        ClearFilmTexture();
        m_IsFilmDirty = false;

        if ( m_FrameSeedType == EFrameSeedType::SampleCount )
        {
            m_FrameSeed = 0;
        }
    }

    if ( m_IsRayTracingJobDirty )
    {
        UpdateRayTracingJob();
        m_IsRayTracingJobDirty = false;
    }

    if ( m_HasValidScene && UpdateResources() )
    {
        m_RayTracingJob.Dispatch();
    }

    if ( m_FrameSeedType != EFrameSeedType::Fixed )
    {
        m_FrameSeed++;
    }
}

void SRenderer::RenderOneFrame()
{
    m_FrameTimer.BeginFrame();

    DispatchRayTracing();

    m_SceneLuminance.Dispatch();

    ID3D11DeviceContext* deviceContext = GetDeviceContext();

    ID3D11RenderTargetView* RTV = m_RenderResultTexture->GetRTV();
    deviceContext->OMSetRenderTargets( 1, &RTV, nullptr );

    D3D11_VIEWPORT viewport = { 0.0f, 0.0f, (float)m_RayTracingConstants.resolutionX, (float)m_RayTracingConstants.resolutionY, 0.0f, 1.0f };
    deviceContext->RSSetViewports( 1, &viewport );

    m_PostProcessing.ExecutePostFX();

    RTV = m_sRGBBackbuffer->GetRTV();
    deviceContext->OMSetRenderTargets( 1, &RTV, nullptr );

    DXGI_SWAP_CHAIN_DESC swapChainDesc;
    GetSwapChain()->GetDesc( &swapChainDesc );
    viewport = { 0.0f, 0.0f, (float)swapChainDesc.BufferDesc.Width, (float)swapChainDesc.BufferDesc.Height, 0.0f, 1.0f };
    deviceContext->RSSetViewports( 1, &viewport );

    XMFLOAT4 clearColor = { 0.0f, 0.0f, 0.0f, 0.0f };
    deviceContext->ClearRenderTargetView( RTV, (float*)&clearColor );

    viewport = { (float)m_RenderViewport.m_TopLeftX, (float)m_RenderViewport.m_TopLeftY, (float)m_RenderViewport.m_Width, (float)m_RenderViewport.m_Height, 0.0f, 1.0f };
    deviceContext->RSSetViewports( 1, &viewport );

    m_PostProcessing.ExecuteCopy();

    ImGUINewFrame();
    OnImGUI();
    ImGui::Render();

    RTV = m_LinearBackbuffer->GetRTV();
    GetDeviceContext()->OMSetRenderTargets( 1, &RTV, nullptr );

    ImGui_ImplDX11_RenderDrawData( ImGui::GetDrawData() );

    RTV = nullptr;
    GetDeviceContext()->OMSetRenderTargets( 1, &RTV, nullptr );

    GetSwapChain()->Present( 0, 0 );
}

bool SRenderer::UpdateResources()
{
    if ( void* address = m_RayTracingFrameConstantBuffer->Map() )
    {
        RayTracingFrameConstants* constants = (RayTracingFrameConstants*)address;
        constants->frameSeed = m_FrameSeed;
        m_RayTracingFrameConstantBuffer->Unmap();
    }
    else
    {
        return false;
    }

    if ( m_IsConstantBufferDirty )
    {
        if ( void* address = m_RayTracingConstantsBuffer->Map() )
        {
            memcpy( address, &m_RayTracingConstants, sizeof( m_RayTracingConstants ) );
            m_RayTracingConstantsBuffer->Unmap();
            m_IsConstantBufferDirty = false;
        }
        else
        {
            return false;
        }
    }

    if ( m_IsLightBufferDirty )
    {
        if ( void* address = m_LightsBuffer->Map() )
        {
            for ( uint32_t i = 0; i < (uint32_t)m_LightSettings.size(); ++i )
            {
                SLightSetting* lightSetting = m_LightSettings.data() + i;
                SLight* light = ( (SLight*)address ) + i;

                XMVECTOR xmPosition = XMLoadFloat3( &lightSetting->position );
                XMVECTOR xmQuat     = XMQuaternionRotationRollPitchYaw( lightSetting->rotation.x, lightSetting->rotation.y, lightSetting->rotation.z );
                XMFLOAT3 size3      = XMFLOAT3( lightSetting->size.x, lightSetting->size.y, 1.0f );
                XMVECTOR xmScale    = XMLoadFloat3( &size3 );
                XMMATRIX xmTransform = XMMatrixAffineTransformation( xmScale, g_XMZero, xmQuat, xmPosition );

                // Shader uses column major
                xmTransform = XMMatrixTranspose( xmTransform ); 
                XMFLOAT4X4 transform44;
                XMStoreFloat4x4( &transform44, xmTransform );
                light->transform = XMFLOAT4X3( (float*)&transform44 );

                light->color = lightSetting->color;

                switch ( lightSetting->lightType )
                {
                case ELightType::Point:
                {
                    light->flags = LIGHT_FLAGS_POINT_LIGHT;
                    break;
                }
                case ELightType::Rectangle:
                {
                    light->flags = 0;
                    break;
                }
                default:
                {
                    light->flags = 0;
                    break;
                }
                }
            }
            m_LightsBuffer->Unmap();
            m_IsLightBufferDirty = false;
        }
        else
        {
            return false;
        }
    }

    if ( m_IsMaterialBufferDirty )
    {
        if ( void* address = m_MaterialsBuffer->Map() )
        {
            memcpy( address, m_Materials.data(), sizeof( Material ) * m_Materials.size() );
            m_MaterialsBuffer->Unmap();
            m_IsMaterialBufferDirty = false;
        }
        else
        {
            return false;
        }
    }

    return true;
}

void SRenderer::ClearFilmTexture()
{
    ID3D11DeviceContext* deviceContext = GetDeviceContext();
    const static float kClearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    deviceContext->ClearRenderTargetView( m_FilmTexture->GetRTV(), kClearColor );
}

void SRenderer::UpdateRayTracingJob()
{
    m_RayTracingJob.m_SamplerStates = { m_UVClampSamplerState.Get() };

    m_RayTracingJob.m_UAVs = { m_FilmTexture->GetUAV() };

    m_RayTracingJob.m_SRVs = {
          m_VerticesBuffer->GetSRV()
        , m_TrianglesBuffer->GetSRV()
        , m_LightsBuffer->GetSRV()
        , m_CookTorranceCompETexture->GetSRV()
        , m_CookTorranceCompEAvgTexture->GetSRV()
        , m_CookTorranceCompInvCDFTexture->GetSRV()
        , m_CookTorranceCompPdfScaleTexture->GetSRV()
        , m_CookTorranceCompEFresnelTexture->GetSRV()
        , m_CookTorranceBSDFEFresnelTexture->GetSRV()
        , m_BVHNodesBuffer ? m_BVHNodesBuffer->GetSRV() : nullptr
        , m_MaterialIdsBuffer->GetSRV()
        , m_MaterialsBuffer->GetSRV()
        , m_EnvironmentTexture->GetSRV()
    };

    m_RayTracingJob.m_ConstantBuffers = { m_RayTracingConstantsBuffer->GetBuffer(), m_RayTracingFrameConstantBuffer->GetBuffer() };

    m_RayTracingJob.m_Shader = m_RayTracingShader[ m_RayTracingKernelIndex ].get();

    m_RayTracingJob.m_DispatchSizeX = (uint32_t)ceil( m_RayTracingConstants.resolutionX / 16.0f );
    m_RayTracingJob.m_DispatchSizeY = (uint32_t)ceil( m_RayTracingConstants.resolutionY / 16.0f );
    m_RayTracingJob.m_DispatchSizeZ = 1;
}

void SRenderer::UpdateRenderViewport( uint32_t backbufferWidth, uint32_t backbufferHeight )
{
    uint32_t renderWidth = m_RayTracingConstants.resolutionX;
    uint32_t renderHeight = m_RayTracingConstants.resolutionY;
    float scale = (float)backbufferWidth / renderWidth;
    float desiredViewportHeight = renderHeight * scale;
    if ( desiredViewportHeight > backbufferHeight )
    {
        scale = (float)backbufferHeight / renderHeight;
    }
    
    m_RenderViewport.m_Width    = uint32_t( renderWidth * scale );
    m_RenderViewport.m_Height   = uint32_t( renderHeight * scale );
    m_RenderViewport.m_TopLeftX = uint32_t( ( backbufferWidth - m_RenderViewport.m_Width ) * 0.5f );
    m_RenderViewport.m_TopLeftY = uint32_t( ( backbufferHeight - m_RenderViewport.m_Height ) * 0.5f );
}

void SRenderer::ResizeBackbuffer( uint32_t backbufferWidth, uint32_t backbufferHeight )
{
    m_sRGBBackbuffer.reset();
    m_LinearBackbuffer.reset();

    ResizeSwapChainBuffers( backbufferWidth, backbufferHeight );

    m_sRGBBackbuffer.reset( GPUTexture::CreateFromSwapChain( DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ) );
    m_LinearBackbuffer.reset( GPUTexture::CreateFromSwapChain() );
}

void SRenderer::CreateEnvironmentTextureFromCurrentFilepath()
{
    std::wstring filepath = StringConversion::UTF8StringToUTF16WString( m_EnvironmentImageFilepath );
    m_EnvironmentTexture.reset( GPUTexture::CreateFromFile( filepath.c_str() ) );
}

bool SRenderer::CompileAndCreateRayTracingKernels( uint32_t traversalStackSize, bool noBVHAccel, bool multipleImportanceSampling )
{
    std::vector<D3D_SHADER_MACRO> rayTracingShaderDefines;

    static const char* s_RayTracingKernelDefines[ s_RayTracingKernelCount ] = { NULL, "OUTPUT_NORMAL", "OUTPUT_TANGENT", "OUTPUT_ALBEDO", "OUTPUT_NEGATIVE_NDOTV", "OUTPUT_BACKFACE" };

    static const uint32_t s_MaxRadix10IntegerBufferLengh = 12;
    char buffer_TraversalStackSize[ s_MaxRadix10IntegerBufferLengh ];
    _itoa( traversalStackSize, buffer_TraversalStackSize, 10 );

    for ( int i = 0; i < s_RayTracingKernelCount; ++i )
    {
        rayTracingShaderDefines.push_back( { "RT_BVH_TRAVERSAL_STACK_SIZE", buffer_TraversalStackSize } );

        if ( noBVHAccel )
        {
            rayTracingShaderDefines.push_back( { "NO_BVH_ACCEL", "0" } );
        }
        if ( multipleImportanceSampling )
        {
            rayTracingShaderDefines.push_back( { "MULTIPLE_IMPORTANCE_SAMPLING", "0" } );
        }
        if ( s_RayTracingKernelDefines[ i ] )
        {
            rayTracingShaderDefines.push_back( { s_RayTracingKernelDefines[ i ], "0" } );
        }
        rayTracingShaderDefines.push_back( { NULL, NULL } );

        m_RayTracingShader[ i ].reset( ComputeShader::CreateFromFile( L"Shaders\\RayTracing.hlsl", rayTracingShaderDefines ) );
        if ( !m_RayTracingShader[ i ] )
            return false;

        rayTracingShaderDefines.clear();
    }

    return true;
}

void SRenderer::OnImGUI()
{
    if ( ImGui::GetIO().KeysDown[ VK_F1 ] && ImGui::GetIO().KeysDownDuration[ VK_F1 ] == 0.0f )
    {
        m_ShowUI = !m_ShowUI;
    }

    if ( !m_ShowUI )
        return;

    {
        ImGui::Begin( "Settings" );
        ImGui::PushItemWidth( ImGui::GetFontSize() * -12 );

        if ( ImGui::CollapsingHeader( "Film" ) )
        {
            if ( ImGui::InputFloat2( "Film Size", (float*)&m_RayTracingConstants.filmSize ) )
                m_IsConstantBufferDirty = true;

            if ( ImGui::DragFloat( "Film Distance", (float*)&m_RayTracingConstants.filmDistance, 0.005f, 0.001f, 1000.0f ) )
                m_IsConstantBufferDirty = true;
        }

        if ( ImGui::CollapsingHeader( "Kernel" ) )
        {
            static const char* s_KernelTypeNames[ s_RayTracingKernelCount ] = { "Path Tracing", "Output Normal", "Output Tangent", "Output Albedo", "Output Negative NdotV", "Output Backface" };
            if ( ImGui::Combo( "Type", &m_RayTracingKernelIndex, s_KernelTypeNames, IM_ARRAYSIZE( s_KernelTypeNames ) ) )
            {
                m_IsRayTracingJobDirty = true;

                m_PostProcessing.SetPostFXDisable( m_RayTracingKernelIndex != 0 );
            }
            if ( m_RayTracingKernelIndex == 0 )
            {
                if ( ImGui::DragInt( "Max Bounce Count", (int*)&m_RayTracingConstants.maxBounceCount, 0.5f, 0, s_MaxRayBounce ) )
                    m_IsConstantBufferDirty = true;
            }
        }

        if ( ImGui::CollapsingHeader( "Samples" ) )
        {
            static const char* s_FrameSeedTypeNames[] = { "Frame Index", "Sample Count", "Fixed" };
            if ( ImGui::Combo( "Frame Seed Type", (int*)&m_FrameSeedType, s_FrameSeedTypeNames, IM_ARRAYSIZE( s_FrameSeedTypeNames ) ) )
            {
                m_IsFilmDirty = true;
            }

            if ( m_FrameSeedType == EFrameSeedType::Fixed )
            {
                if ( ImGui::InputInt( "Frame Seed", (int*)&m_FrameSeed, 1 ) )
                {
                    m_IsFilmDirty = true;
                }
            }
        }

        if ( m_RayTracingKernelIndex == 0 )
        {
            if ( ImGui::CollapsingHeader( "Environment" ) )
            {
                ImGui::SetColorEditOptions( ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR );
                if ( ImGui::ColorEdit3( "Background Color", (float*)&m_RayTracingConstants.background ) )
                    m_IsConstantBufferDirty = true;

                ImGui::InputText( "Image File", const_cast<char*>( m_EnvironmentImageFilepath.c_str() ), m_EnvironmentImageFilepath.size(), ImGuiInputTextFlags_ReadOnly );
                if ( ImGui::Button( "Browse##BrowseEnvImage" ) )
                {
                    OPENFILENAMEA ofn;
                    char filepath[ MAX_PATH ];
                    ZeroMemory( &ofn, sizeof( ofn ) );
                    ofn.lStructSize = sizeof( ofn );
                    ofn.hwndOwner = m_hWnd;
                    ofn.lpstrFile = filepath;
                    ofn.lpstrFile[ 0 ] = '\0';
                    ofn.nMaxFile = sizeof( filepath );
                    ofn.lpstrFilter = "DDS\0*.DDS\0";
                    ofn.nFilterIndex = 1;
                    ofn.lpstrFileTitle = NULL;
                    ofn.nMaxFileTitle = 0;
                    ofn.lpstrInitialDir = NULL;
                    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

                    if ( GetOpenFileNameA( &ofn ) == TRUE )
                    {
                        m_EnvironmentImageFilepath = filepath;
                        CreateEnvironmentTextureFromCurrentFilepath();
                        m_IsRayTracingJobDirty = true;
                    }
                }
            }

            m_SceneLuminance.OnImGUI();
            m_PostProcessing.OnImGUI();
        }

        ImGui::PopItemWidth();
        ImGui::End();
    }

    {
        ImGui::Begin( "Scene", (bool*)0, ImGuiWindowFlags_MenuBar );

        if ( ImGui::BeginMenuBar() )
        {
            if ( ImGui::MenuItem( "Load" ) )
            {
                OPENFILENAMEA ofn;
                char filepath[ MAX_PATH ];
                ZeroMemory( &ofn, sizeof( ofn ) );
                ofn.lStructSize = sizeof( ofn );
                ofn.hwndOwner = m_hWnd;
                ofn.lpstrFile = filepath;
                ofn.lpstrFile[ 0 ] = '\0';
                ofn.nMaxFile = sizeof( filepath );
                ofn.lpstrFilter = "Wavefront OBJ\0*.OBJ\0";
                ofn.nFilterIndex = 1;
                ofn.lpstrFileTitle = NULL;
                ofn.nMaxFileTitle = 0;
                ofn.lpstrInitialDir = NULL;
                ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

                if ( GetOpenFileNameA( &ofn ) == TRUE )
                {
                    m_HasValidScene = ResetScene( filepath );
                }
            }
            if ( ImGui::BeginMenu( "Edit" ) )
            {
                if ( ImGui::BeginMenu( "Create" ) )
                {
                    ELightType lightType = ELightType::Point;
                    bool menuItemClicked = false;
                    if ( ImGui::MenuItem( "Point Light", "", false, m_LightSettings.size() < s_MaxLightsCount ) )
                    {
                        lightType = ELightType::Point;
                        menuItemClicked = true;
                    }
                    if ( ImGui::MenuItem( "Rectangle Light", "", false, m_LightSettings.size() < s_MaxLightsCount ) )
                    {
                        lightType = ELightType::Rectangle;
                        menuItemClicked = true;
                    }

                    if ( menuItemClicked )
                    {
                        m_LightSettings.emplace_back();
                        m_LightSettings.back().color = XMFLOAT3( 1.0f, 1.0f, 1.0f );
                        m_LightSettings.back().position = XMFLOAT3( 0.0f, 0.0f, 0.0f );
                        m_LightSettings.back().rotation = XMFLOAT3( 0.0f, 0.0f, 0.0f );
                        m_LightSettings.back().size = XMFLOAT2( 1.0f, 1.0f );
                        m_LightSettings.back().lightType = lightType;
                        m_RayTracingConstants.lightCount++;
                        m_IsConstantBufferDirty = true;
                        m_IsLightBufferDirty = true;
                    }

                    ImGui::EndMenu();
                }
                if ( ImGui::MenuItem( "Delete", "", false, m_SceneObjectSelection.m_LightSelectionIndex != -1 ) )
                {
                    m_LightSettings.erase( m_LightSettings.begin() + m_SceneObjectSelection.m_LightSelectionIndex );
                    m_SceneObjectSelection.m_LightSelectionIndex = -1;
                    m_RayTracingConstants.lightCount--;
                    m_IsConstantBufferDirty = true;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        if ( ImGui::CollapsingHeader( "Camera" ) )
        {
            bool isSelected = m_SceneObjectSelection.m_IsCameraSelected;
            if ( ImGui::Selectable( "Preview Camera", isSelected ) )
            {
                m_SceneObjectSelection.SelectCamera();
            }
        }

        if ( ImGui::CollapsingHeader( "Lights" ) )
        {
            char label[ 32 ];
            for ( size_t iLight = 0; iLight < m_LightSettings.size(); ++iLight )
            {
                bool isSelected = ( iLight == m_SceneObjectSelection.m_LightSelectionIndex );
                sprintf( label, "Light %d", uint32_t( iLight ) );
                if ( ImGui::Selectable( label, isSelected ) )
                {
                    m_SceneObjectSelection.SelectLight( (int)iLight );
                }
            }
        }

        if ( ImGui::CollapsingHeader( "Materials" ) )
        {
            for ( size_t iMaterial = 0; iMaterial < m_Materials.size(); ++iMaterial )
            {
                bool isSelected = ( iMaterial == m_SceneObjectSelection.m_MaterialSelectionIndex );
                if ( ImGui::Selectable( m_MaterialNames[ iMaterial ].c_str(), isSelected ) )
                {
                    m_SceneObjectSelection.SelectMaterial( (int)iMaterial );
                }
            }
        }

        ImGui::End();
    }

    {
        ImGui::Begin( "Inspector" );

        ImGui::PushItemWidth( ImGui::GetFontSize() * -9 );

        if ( m_SceneObjectSelection.m_LightSelectionIndex >= 0 )
        {
            ImGui::SetColorEditOptions( ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR );
            if ( m_SceneObjectSelection.m_LightSelectionIndex < m_LightSettings.size() )
            {
                SLightSetting* selection = m_LightSettings.data() + m_SceneObjectSelection.m_LightSelectionIndex;

                if ( ImGui::DragFloat3( "Position", (float*)&selection->position, 1.0f ) )
                    m_IsLightBufferDirty = true;

                if ( selection->lightType == ELightType::Rectangle )
                {
                    XMFLOAT3 eulerAnglesDeg;
                    eulerAnglesDeg.x = XMConvertToDegrees( selection->rotation.x );
                    eulerAnglesDeg.y = XMConvertToDegrees( selection->rotation.y );
                    eulerAnglesDeg.z = XMConvertToDegrees( selection->rotation.z );
                    if ( ImGui::DragFloat3( "Rotation", (float*)&eulerAnglesDeg, 1.0f ) )
                    {
                        selection->rotation.x = XMConvertToRadians( eulerAnglesDeg.x );
                        selection->rotation.y = XMConvertToRadians( eulerAnglesDeg.y );
                        selection->rotation.z = XMConvertToRadians( eulerAnglesDeg.z );
                        m_IsLightBufferDirty = true;
                    }
                }

                static const char* s_LightTypeNames[] = { "Point", "Rectangle" };
                if ( ImGui::Combo( "Type", (int*)&selection->lightType, s_LightTypeNames, IM_ARRAYSIZE( s_LightTypeNames ) ) )
                {
                    m_IsLightBufferDirty = true;
                }

                if ( selection->lightType == ELightType::Rectangle )
                {
                    if ( ImGui::DragFloat2( "Size", (float*)&selection->size, 0.1f, 0.0001f, 10000000.0f ) )
                        m_IsLightBufferDirty = true;
                }

                if ( ImGui::ColorEdit3( "Color", (float*)&selection->color ) )
                    m_IsLightBufferDirty = true;
            }
        }
        else if ( m_SceneObjectSelection.m_MaterialSelectionIndex >= 0 )
        {
            if ( m_SceneObjectSelection.m_MaterialSelectionIndex < m_Materials.size() )
            {
                Material* selection = m_Materials.data() + m_SceneObjectSelection.m_MaterialSelectionIndex;
                ImGui::SetColorEditOptions( ImGuiColorEditFlags_Float );
                if ( ImGui::ColorEdit3( "Albedo", (float*)&selection->albedo ) )
                    m_IsMaterialBufferDirty = true;
                ImGui::SetColorEditOptions( ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR );
                if ( ImGui::ColorEdit3( "Emission", (float*)&selection->emission ) )
                    m_IsMaterialBufferDirty = true;
                if ( ImGui::DragFloat( "Roughness", &selection->roughness, 0.01f, 0.0f, 1.0f ) )
                    m_IsMaterialBufferDirty = true;
                if ( ImGui::DragFloat( "IOR", &selection->ior, 0.01f, 1.0f, 3.0f ) )
                    m_IsMaterialBufferDirty = true;
                if ( ImGui::DragFloat2( "Texture Tiling", (float*)&selection->texTiling, 0.01f, 0.0f, 100000.0f ) )
                    m_IsMaterialBufferDirty = true;
                if ( ImGui::CheckboxFlags( "Albedo Texture", (int*)&selection->flags, MATERIAL_FLAG_ALBEDO_TEXTURE ) )
                    m_IsMaterialBufferDirty = true;
                if ( ImGui::CheckboxFlags( "Emission Texture", (int*)&selection->flags, MATERIAL_FLAG_EMISSION_TEXTURE ) )
                    m_IsMaterialBufferDirty = true;
                if ( ImGui::CheckboxFlags( "Roughness Texture", (int*)&selection->flags, MATERIAL_FLAG_ROUGHNESS_TEXTURE ) )
                    m_IsMaterialBufferDirty = true;
            }
        }
        else if ( m_SceneObjectSelection.m_IsCameraSelected )
        {
            m_Camera.OnImGUI();
        }

        ImGui::End();
    }

    {
        ImGui::Begin( "Render Stats." );

        ImGui::Text( "MIS: %s", m_IsMultipleImportanceSamplingEnabled ? "On" : "Off" );
        ImGui::Text( "Average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate );

        ImGui::End();
    }
}
