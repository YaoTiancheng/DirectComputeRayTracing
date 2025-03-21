#include "stdafx.h"
#include "DirectComputeRayTracing.h"
#include "D3D12Adapter.h"
#include "D3D12GPUDescriptorHeap.h"
#include "ScopedRenderAnnotation.h"
#include "PathTracer.h"
#include "RenderContext.h"
#include "MessageBox.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx12.h"
#include "imgui/imgui_impl_win32.h"

using namespace DirectX;
using SRenderer = CDirectComputeRayTracing::SRenderer;

static bool DragFloat3RadianInDegree( const char* label, float v[3], float v_speed = 1.f, float v_min = 0.f, float v_max = 0.f, const char* format = "%.3f", ImGuiSliderFlags flags = 0 )
{
    XMFLOAT3 eulerAnglesDeg;
    eulerAnglesDeg.x = XMConvertToDegrees( v[ 0 ] );
    eulerAnglesDeg.y = XMConvertToDegrees( v[ 1 ] );
    eulerAnglesDeg.z = XMConvertToDegrees( v[ 2 ] );
    if ( ImGui::DragFloat3( label, (float*)&eulerAnglesDeg, v_speed, v_min, v_max, format, flags ) )
    { 
        v[ 0 ] = XMConvertToRadians( eulerAnglesDeg.x );
        v[ 1 ] = XMConvertToRadians( eulerAnglesDeg.y );
        v[ 2 ] = XMConvertToRadians( eulerAnglesDeg.z );
        return true;
    }
    return false;
}

static void AllocImGuiDescriptor( ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_desc_handle )
{
    SD3D12GPUDescriptorHeapHandle descriptorHandle = D3D12Adapter::GetGPUDescriptorHeap()->GetReserved( 0 );
    *out_cpu_desc_handle = descriptorHandle.m_CPU;
    *out_gpu_desc_handle = descriptorHandle.m_GPU;
}

static void FreeImGuiDescriptor( ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE gpu_desc_handle )
{
    // Do nothing
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam );

bool SRenderer::ProcessImGuiWindowMessage( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam )
{
    return ImGui_ImplWin32_WndProcHandler( hWnd, msg, wParam, lParam );
}

bool SRenderer::InitImGui( HWND hWnd )
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

    ImGui_ImplDX12_InitInfo initInfo = {};
    initInfo.Device = D3D12Adapter::GetDevice();
    initInfo.CommandQueue = D3D12Adapter::GetCommandQueue();
    initInfo.NumFramesInFlight = D3D12Adapter::GetBackbufferCount();
    initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    initInfo.SrvDescriptorHeap = D3D12Adapter::GetGPUDescriptorHeap()->GetD3DHeap();
    initInfo.SrvDescriptorAllocFn = AllocImGuiDescriptor;
    initInfo.SrvDescriptorFreeFn = FreeImGuiDescriptor;
    if ( !ImGui_ImplDX12_Init( &initInfo ) )
    {
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    return true;
}

void SRenderer::ShutdownImGui()
{
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

void SRenderer::DrawImGui( ID3D12GraphicsCommandList* commandList )
{
    ImGui::Render();

    SCOPED_RENDER_ANNOTATION( commandList, L"ImGUI" );
    ImGui_ImplDX12_RenderDrawData( ImGui::GetDrawData(), commandList );
}

void SRenderer::OnImGUI( SRenderContext* renderContext )
{
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    if ( ImGui::IsKeyPressed( ImGuiKey_F1, false ) )
    {
        m_ShowUI = !m_ShowUI;
    }
    if ( ImGui::IsKeyPressed( ImGuiKey_F2, false ) )
    {
        m_ShowRayTracingUI = !m_ShowRayTracingUI;
    }

    if ( !m_ShowUI )
        return;

    if ( m_Scene.m_HasValidScene )
    {
        ImGui::Begin( "Settings" );
        ImGui::PushItemWidth( ImGui::GetFontSize() * -15 );

        if ( ImGui::CollapsingHeader( "General" ) )
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

            ImGui::DragInt( "Resolution Width", (int*)&m_NewResolutionWidth, 16, 16, 4096, "%d", ImGuiSliderFlags_AlwaysClamp );
            ImGui::DragInt( "Resolution Height", (int*)&m_NewResolutionHeight, 16, 16, 4096, "%d", ImGuiSliderFlags_AlwaysClamp );
            if ( m_Scene.m_ResolutionWidth != m_NewResolutionWidth || m_Scene.m_ResolutionHeight != m_NewResolutionHeight )
            {
                if ( ImGui::Button( "Apply##ApplyResolutionChange" ) )
                {
                    m_Scene.m_ResolutionWidth = m_NewResolutionWidth;
                    m_Scene.m_ResolutionHeight = m_NewResolutionHeight;
                    m_Scene.RecreateFilmTextures();
                    HandleFilmResolutionChange();
                    m_IsFilmDirty = true;
                }
            }

            ImGui::DragInt( "Small Resolution Width", (int*)&m_SmallResolutionWidth, 16, 16, m_Scene.m_ResolutionWidth, "%d", ImGuiSliderFlags_AlwaysClamp );
            ImGui::DragInt( "Small Resolution Height", (int*)&m_SmallResolutionHeight, 16, 16, m_Scene.m_ResolutionHeight, "%d", ImGuiSliderFlags_AlwaysClamp );

            uint32_t lastActivePathTracerIndex = m_ActivePathTracerIndex;
            static const char* s_PathTracerNames[] = { "Megakernel Path Tracer", "Wavefront Path Tracer" };
            if ( ImGui::Combo( "Path Tracer", (int*)&m_ActivePathTracerIndex, s_PathTracerNames, IM_ARRAYSIZE( s_PathTracerNames ) ) )
            {
                m_PathTracer[ lastActivePathTracerIndex ]->Destroy();
                m_PathTracer[ m_ActivePathTracerIndex ]->Create();
                m_PathTracer[ m_ActivePathTracerIndex ]->ResetImage();
                if ( m_Scene.m_HasValidScene )
                {
                    m_PathTracer[ m_ActivePathTracerIndex ]->OnSceneLoaded();
                }
                m_IsFilmDirty = true;
            }
        }

        if ( ImGui::CollapsingHeader( "Scene" ) )
        {
            if ( ImGui::DragInt( "Max Bounce Count", (int*)&m_Scene.m_MaxBounceCount, 0.5f, 0, m_Scene.s_MaxRayBounce ) )
            {
                m_IsFilmDirty = true;
            }

            static const char* s_FilterNames[] = { "Box", "Triangle", "Gaussian", "Mitchell", "Lanczos Sinc" };
            if ( ImGui::Combo( "Filter", (int*)&m_Scene.m_Filter, s_FilterNames, IM_ARRAYSIZE( s_FilterNames ) ) )
            {
                m_IsFilmDirty = true;
            }

            if ( ImGui::DragFloat( "Filter Radius", &m_Scene.m_FilterRadius, 0.1f, 0.001f, 16.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp ) )
            {
                m_IsFilmDirty = true;
            }

            if ( m_Scene.m_Filter == EFilter::Gaussian )
            {
                if ( ImGui::DragFloat( "Alpha", &m_Scene.m_GaussianFilterAlpha, 0.005f, 0.0f, 100.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp ) )
                {
                    m_IsFilmDirty = true;
                }
            }
            else if ( m_Scene.m_Filter == EFilter::Mitchell )
            {
                if ( ImGui::DragFloat( "B", &m_Scene.m_MitchellB, 0.01f, 0.0f, 100.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp ) )
                {
                    m_IsFilmDirty = true;
                }
                if ( ImGui::DragFloat( "C", &m_Scene.m_MitchellC, 0.01f, 0.0f, 100.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp ) )
                {
                    m_IsFilmDirty = true;
                }
            }
            else if ( m_Scene.m_Filter == EFilter::LanczosSinc )
            {
                if ( ImGui::DragInt( "Tau", (int*)&m_Scene.m_LanczosSincTau, 1, 1, 100, "%d", ImGuiSliderFlags_AlwaysClamp ) )
                {
                    m_IsFilmDirty = true;
                }
            }

            if ( ImGui::Checkbox( "GGX VNDF Sampling", &m_Scene.m_IsGGXVNDFSamplingEnabled ) )
            {
                m_PathTracer[ m_ActivePathTracerIndex ]->OnSceneLoaded();
                m_IsFilmDirty = true;
            }

            if ( ImGui::Checkbox( "Traverse BVH Front-to-back", &m_Scene.m_TraverseBVHFrontToBack ) )
            {
                m_PathTracer[ m_ActivePathTracerIndex ]->OnSceneLoaded();
                m_IsFilmDirty = true;
            }

            if ( ImGui::Checkbox( "Lights Visble to Camera", &m_Scene.m_IsLightVisible ) )
            {
                m_PathTracer[ m_ActivePathTracerIndex ]->OnSceneLoaded();
                m_IsFilmDirty = true;
            }
        }

        m_PathTracer[ m_ActivePathTracerIndex ]->OnImGUI();

        m_PostProcessing.OnImGUI();

        ImGui::PopItemWidth();
        ImGui::End();
    }

    {
        ImGui::Begin( "Scene", (bool*)0, ImGuiWindowFlags_MenuBar );

        if ( ImGui::BeginMenuBar() )
        {
            if ( ImGui::BeginMenu( "File" ) )
            {
                bool loadScene = ImGui::MenuItem( "Load Scene" );
                bool resetAndLoadScene = ImGui::MenuItem( "Reset & Load Scene" );
                if ( loadScene || resetAndLoadScene )
                {
                    OPENFILENAMEA ofn;
                    char filepath[ MAX_PATH ];
                    ZeroMemory( &ofn, sizeof( ofn ) );
                    ofn.lStructSize = sizeof( ofn );
                    ofn.hwndOwner = m_hWnd;
                    ofn.lpstrFile = filepath;
                    ofn.lpstrFile[ 0 ] = '\0';
                    ofn.nMaxFile = sizeof( filepath );
                    ofn.lpstrFilter = "All Scene Files (*.obj;*.xml)\0*.OBJ;*.XML\0";
                    ofn.nFilterIndex = 1;
                    ofn.lpstrFileTitle = NULL;
                    ofn.nMaxFileTitle = 0;
                    ofn.lpstrInitialDir = NULL;
                    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

                    if ( GetOpenFileNameA( &ofn ) == TRUE )
                    {
                        LoadScene( filepath, resetAndLoadScene );
                    }
                }
                ImGui::EndMenu();
            }
            if ( ImGui::BeginMenu( "Edit", m_Scene.m_HasValidScene ) )
            {
                if ( ImGui::BeginMenu( "Create" ) )
                {
                    if ( ImGui::MenuItem( "Point Light", "", false, m_Scene.GetLightCount() < m_Scene.s_MaxLightsCount ) )
                    {
                        m_Scene.m_PunctualLights.emplace_back();
                        SPunctualLight& newLight = m_Scene.m_PunctualLights.back();
                        newLight.m_Color = XMFLOAT3( 1.0f, 1.0f, 1.0f );
                        newLight.m_Position = XMFLOAT3( 0.0f, 0.0f, 0.0f );
                        newLight.m_IsDirectionalLight = false;
                        m_IsLightGPUBufferDirty = true;
                        m_IsFilmDirty = true;
                    }
                    if ( ImGui::MenuItem( "Directional Light", "", false, m_Scene.GetLightCount() < m_Scene.s_MaxLightsCount ) )
                    {
                        m_Scene.m_PunctualLights.emplace_back();
                        SPunctualLight& newLight = m_Scene.m_PunctualLights.back();
                        newLight.m_Color = XMFLOAT3( 1.0f, 1.0f, 1.0f );
                        newLight.SetEulerAnglesFromDirection( XMFLOAT3( 0.f, -1.f, 0.f ) );
                        newLight.m_IsDirectionalLight = true;
                        m_IsLightGPUBufferDirty = true;
                        m_IsFilmDirty = true;
                    }
                    if ( ImGui::MenuItem( "Environment Light", "", false, m_Scene.m_EnvironmentLight == nullptr && m_Scene.GetLightCount() < m_Scene.s_MaxLightsCount ) )
                    {
                        m_Scene.m_EnvironmentLight = std::make_shared<SEnvironmentLight>();
                        m_Scene.m_EnvironmentLight->m_Color = XMFLOAT3( 1.0f, 1.0f, 1.0f );
                        m_IsLightGPUBufferDirty = true;
                        m_IsFilmDirty = true;
                    }
                    ImGui::EndMenu();
                }
                if ( ImGui::MenuItem( "Delete", "", false, m_Scene.m_ObjectSelection.m_PunctualLightSelectionIndex != -1 || m_Scene.m_ObjectSelection.m_IsEnvironmentLightSelected ) )
                {
                    if ( m_Scene.m_ObjectSelection.m_PunctualLightSelectionIndex != -1 )
                    {
                        m_Scene.m_PunctualLights.erase( m_Scene.m_PunctualLights.begin() + m_Scene.m_ObjectSelection.m_PunctualLightSelectionIndex );
                        m_Scene.m_ObjectSelection.m_PunctualLightSelectionIndex = -1;
                    }
                    else
                    {
                        bool hadEnvironmentTexture = m_Scene.m_EnvironmentLight->m_Texture.Get() != nullptr;
                        m_Scene.m_EnvironmentLight.reset();
                        if ( hadEnvironmentTexture )
                        {
                            // Allow the path tracer switching to a kernel without sampling the environment texture
                            m_PathTracer[ m_ActivePathTracerIndex ]->OnSceneLoaded();
                        }

                        m_Scene.m_ObjectSelection.m_IsEnvironmentLightSelected = false;
                    }
                    m_IsLightGPUBufferDirty = true;
                    m_IsFilmDirty = true;;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        if ( ImGui::CollapsingHeader( "Camera" ) )
        {
            bool isSelected = m_Scene.m_ObjectSelection.m_IsCameraSelected;
            if ( ImGui::Selectable( "Preview Camera", isSelected ) )
            {
                m_Scene.m_ObjectSelection.SelectCamera();
            }
        }

        if ( ImGui::CollapsingHeader( "Punctual Lights" ) )
        {
            char label[ 32 ];
            for ( size_t iLight = 0; iLight < m_Scene.m_PunctualLights.size(); ++iLight )
            {
                bool isSelected = ( iLight == m_Scene.m_ObjectSelection.m_PunctualLightSelectionIndex );
                sprintf( label, "Light %d", uint32_t( iLight ) );
                if ( ImGui::Selectable( label, isSelected ) )
                {
                    m_Scene.m_ObjectSelection.SelectPunctualLight( (int)iLight );
                }
            }
        }

        if ( ImGui::CollapsingHeader( "Environment Light" ) )
        {
            if ( m_Scene.m_EnvironmentLight )
            {
                if ( ImGui::Selectable( "Light##EnvLight", m_Scene.m_ObjectSelection.m_IsEnvironmentLightSelected ) )
                {
                    m_Scene.m_ObjectSelection.SelectEnvironmentLight();
                }
            }
        }

        if ( ImGui::CollapsingHeader( "Materials" ) )
        {
            for ( size_t iMaterial = 0; iMaterial < m_Scene.m_Materials.size(); ++iMaterial )
            {
                bool isSelected = ( iMaterial == m_Scene.m_ObjectSelection.m_MaterialSelectionIndex );
                ImGui::PushID( (int)iMaterial );
                if ( ImGui::Selectable( m_Scene.m_MaterialNames[ iMaterial ].c_str(), isSelected ) )
                {
                    m_Scene.m_ObjectSelection.SelectMaterial( (int)iMaterial );
                }
                ImGui::PopID();
            }
        }

        ImGui::End();
    }

    {
        ImGui::Begin( "Inspector" );

        ImGui::PushItemWidth( ImGui::GetFontSize() * -9 );

        if ( m_Scene.m_ObjectSelection.m_PunctualLightSelectionIndex >= 0 )
        {
            ImGui::SetColorEditOptions( ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR );
            if ( m_Scene.m_ObjectSelection.m_PunctualLightSelectionIndex < m_Scene.m_PunctualLights.size() )
            {
                SPunctualLight* selection = m_Scene.m_PunctualLights.data() + m_Scene.m_ObjectSelection.m_PunctualLightSelectionIndex;

                if ( selection->m_IsDirectionalLight )
                {
                    if ( DragFloat3RadianInDegree( "Euler Angles", (float*)&selection->m_EulerAngles, 1.f ) )
                        m_IsLightGPUBufferDirty = true;

                    XMFLOAT3 direction = selection->CalculateDirection();
                    ImGui::LabelText( "Direction", "%.3f, %.3f, %.3f", direction.x, direction.y, direction.z );
                }
                else
                {
                    if ( ImGui::DragFloat3( "Position", (float*)&selection->m_Position, 1.0f ) )
                        m_IsLightGPUBufferDirty = true;
                }

                if ( ImGui::ColorEdit3( "Color", (float*)&selection->m_Color ) )
                    m_IsLightGPUBufferDirty = true;
            }
        }
        else if ( m_Scene.m_ObjectSelection.m_IsEnvironmentLightSelected )
        {
            ImGui::SetColorEditOptions( ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR );
            if ( ImGui::ColorEdit3( "Radiance", (float*)&m_Scene.m_EnvironmentLight->m_Color ) )
                m_IsLightGPUBufferDirty = true;

            ImGui::InputText( "Image File", const_cast<char*>( m_Scene.m_EnvironmentLight->m_TextureFileName.c_str() ), m_Scene.m_EnvironmentLight->m_TextureFileName.size(), ImGuiInputTextFlags_ReadOnly );
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
                    m_Scene.m_EnvironmentLight->m_TextureFileName = filepath;
                    bool hasEnvTexturePreviously = m_Scene.m_EnvironmentLight->m_Texture.Get() != nullptr;
                    m_Scene.m_EnvironmentLight->CreateTextureFromFile();
                    bool hasEnvTextureCurrently = m_Scene.m_EnvironmentLight->m_Texture.Get() != nullptr;
                    if ( hasEnvTexturePreviously != hasEnvTextureCurrently )
                    {
                        m_PathTracer[ m_ActivePathTracerIndex ]->OnSceneLoaded();
                    }
                    m_IsFilmDirty = true;
                }
            }
            if ( m_Scene.m_EnvironmentLight->m_Texture )
            {
                ImGui::SameLine();
                if ( ImGui::Button( "Clear##ClearEnvImage" ) )
                {
                    m_Scene.m_EnvironmentLight->m_TextureFileName = "";
                    m_Scene.m_EnvironmentLight->m_Texture.Reset();
                    m_PathTracer[ m_ActivePathTracerIndex ]->OnSceneLoaded();
                    m_IsFilmDirty = true;
                }
            }
        }
        else if ( m_Scene.m_ObjectSelection.m_MaterialSelectionIndex >= 0 )
        {
            if ( m_Scene.m_ObjectSelection.m_MaterialSelectionIndex < m_Scene.m_Materials.size() )
            {
                SMaterial* selection = m_Scene.m_Materials.data() + m_Scene.m_ObjectSelection.m_MaterialSelectionIndex;

                static const char* s_MaterialTypeNames[] = { "Diffuse", "Plastic", "Conductor", "Dielectric" };
                if ( ImGui::Combo( "Type", (int*)&selection->m_MaterialType, s_MaterialTypeNames, IM_ARRAYSIZE( s_MaterialTypeNames ) ) )
                {
                    if ( selection->m_MaterialType != EMaterialType::Conductor )
                    {
                        // Reclamp IOR to above 1.0 when material is not conductor
                        selection->m_IOR.x = std::max( 1.0f, selection->m_IOR.x );
                    }
                    m_IsMaterialGPUBufferDirty = true;
                }

                if ( selection->m_MaterialType == EMaterialType::Diffuse || selection->m_MaterialType == EMaterialType::Plastic )
                {
                    ImGui::SetColorEditOptions( ImGuiColorEditFlags_Float );
                    m_IsMaterialGPUBufferDirty |= ImGui::ColorEdit3( "Albedo", (float*)&selection->m_Albedo );
                    m_IsMaterialGPUBufferDirty |= ImGui::Checkbox( "Albedo Texture", &selection->m_HasAlbedoTexture );
                }

                if ( selection->m_MaterialType != EMaterialType::Diffuse )
                {
                    ImGui::SetColorEditOptions( ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR );
                    m_IsMaterialGPUBufferDirty |= ImGui::DragFloat( "Roughness", &selection->m_Roughness, 0.01f, 0.0f, 1.0f );
                    m_IsMaterialGPUBufferDirty |= ImGui::Checkbox( "Roughness Texture", &selection->m_HasRoughnessTexture );
                    m_IsMaterialGPUBufferDirty |= ImGui::Checkbox( "Multiscattering", &selection->m_Multiscattering );
                }

                if ( selection->m_MaterialType == EMaterialType::Conductor )
                {
                    m_IsMaterialGPUBufferDirty |= ImGui::DragFloat3( "eta", (float*)&selection->m_IOR, 0.01f, 0.0f, MAX_MATERIAL_ETA, "%.3f", ImGuiSliderFlags_AlwaysClamp );              
                    m_IsMaterialGPUBufferDirty |= ImGui::DragFloat3( "k", (float*)&selection->m_K, 0.01f, 0.0f, MAX_MATERIAL_K, "%.3f", ImGuiSliderFlags_AlwaysClamp );
                }
                else if ( selection->m_MaterialType != EMaterialType::Diffuse )
                {
                    m_IsMaterialGPUBufferDirty |= ImGui::DragFloat( "IOR", (float*)&selection->m_IOR, 0.01f, 1.0f, MAX_MATERIAL_IOR, "%.3f", ImGuiSliderFlags_AlwaysClamp );
                }

                if ( selection->m_MaterialType != EMaterialType::Dielectric )
                {
                    m_IsMaterialGPUBufferDirty |= ImGui::Checkbox( "Two Sided", &selection->m_IsTwoSided );
                }

                m_IsMaterialGPUBufferDirty |= ImGui::DragFloat2( "Texture Tiling", (float*)&selection->m_Tiling, 0.01f, 0.0f, 100000.0f );
            }
        }
        else if ( m_Scene.m_ObjectSelection.m_IsCameraSelected )
        {
            m_Scene.m_Camera.OnImGUI();

            if ( ImGui::DragFloat( "Film Width", &m_Scene.m_FilmSize.x, 0.005f, 0.001f, 999.f, "%.3f", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoRoundToFormat ) )
            {
                m_Scene.m_FilmSize.y = m_Scene.m_FilmSize.x / m_Scene.m_ResolutionWidth * m_Scene.m_ResolutionHeight;
                m_IsFilmDirty = true;
            }
            if ( ImGui::DragFloat( "Film Height", &m_Scene.m_FilmSize.y, 0.005f, 0.001f, 999.f, "%.3f", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoRoundToFormat ) )
            {
                m_Scene.m_FilmSize.x = m_Scene.m_FilmSize.y / m_Scene.m_ResolutionHeight * m_Scene.m_ResolutionWidth;
                m_IsFilmDirty = true;
            }

            static const char* s_CameraTypeNames[] = { "PinHole", "ThinLens" };
            if ( ImGui::Combo( "Type", (int*)&m_Scene.m_CameraType, s_CameraTypeNames, IM_ARRAYSIZE( s_CameraTypeNames ) ) )
                m_IsFilmDirty = true;
            
            if ( m_Scene.m_CameraType == ECameraType::PinHole )
            {
                float fovDeg = DirectX::XMConvertToDegrees( m_Scene.m_FoVX );
                if ( ImGui::DragFloat( "FoV", (float*)&fovDeg, 1.f, 0.00001f, 179.9f, "%.2f", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoRoundToFormat ) )
                { 
                    m_Scene.m_FoVX = DirectX::XMConvertToRadians( fovDeg );
                    m_IsFilmDirty = true;
                }
            }
            else
            { 
                if ( ImGui::DragFloat( "Focal Length", (float*)&m_Scene.m_FocalLength, 0.000001f, 0.000001f, 1000.0f, "%.5f", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoRoundToFormat ) )
                    m_IsFilmDirty = true;
            
                if ( ImGui::DragFloat( "Focal Distance", (float*)&m_Scene.m_FocalDistance, 0.005f, 0.000001f, m_Scene.s_MaxFocalDistance, "%.5f", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoRoundToFormat ) )
                    m_IsFilmDirty = true;

                if ( ImGui::DragFloat( "Aperture(f-number)", &m_Scene.m_RelativeAperture, 0.1f, 0.01f, 1000.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp ) )
                    m_IsFilmDirty = true;

                if ( ImGui::DragInt( "Aperture Blade Count", (int*)&m_Scene.m_ApertureBladeCount, 1.0f, 2, 16, "%d", ImGuiSliderFlags_AlwaysClamp ) )
                    m_IsFilmDirty = true;

                float apertureRotationDeg = DirectX::XMConvertToDegrees( m_Scene.m_ApertureRotation );
                if ( ImGui::DragFloat( "Aperture Rotation", &apertureRotationDeg, 1.0f, 0.0f, 360.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp ) )
                {
                    m_Scene.m_ApertureRotation = DirectX::XMConvertToRadians( apertureRotationDeg );
                    m_IsFilmDirty = true;
                }
            }

            ImGui::DragFloat( "Shutter Time", &m_Scene.m_ShutterTime, 0.001f, 0.001f, 100000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp );
            ImGui::DragFloat( "ISO", &m_Scene.m_ISO, 50.f, 50.f, 100000.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp );
        }

        ImGui::End();
    }

    if ( m_Scene.m_HasValidScene )
    {
        ImGui::Begin( "Render Stats." );

        ImGui::Text( "Film Resolution: %dx%d", renderContext->m_CurrentResolutionWidth, renderContext->m_CurrentResolutionHeight );
        ImGui::Text( "Render Viewport: %dx%d", m_RenderViewport.m_Width, m_RenderViewport.m_Height );
        ImGui::Text( "Average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate );
        ImGui::Text( "SPP: %d", m_SPP );

        {
            POINT pos;
            ::GetCursorPos( &pos );
            ::ScreenToClient( m_hWnd, &pos );

            m_CursorPixelPosOnRenderViewport[ 0 ] = (uint32_t)std::clamp<int>( (int)pos.x - (int)m_RenderViewport.m_TopLeftX, 0, (int)m_RenderViewport.m_Width );
            m_CursorPixelPosOnRenderViewport[ 1 ] = (uint32_t)std::clamp<int>( (int)pos.y - (int)m_RenderViewport.m_TopLeftY, 0, (int)m_RenderViewport.m_Height );

            float filmPixelPerRenderViewportPixelX = (float)m_Scene.m_ResolutionWidth / m_RenderViewport.m_Width;
            float filmPixelPerRenderViewportPixelY = (float)m_Scene.m_ResolutionHeight / m_RenderViewport.m_Height;
            m_CursorPixelPosOnFilm[ 0 ] = (uint32_t)std::floorf( filmPixelPerRenderViewportPixelX * m_CursorPixelPosOnRenderViewport[ 0 ] );
            m_CursorPixelPosOnFilm[ 1 ] = (uint32_t)std::floorf( filmPixelPerRenderViewportPixelY * m_CursorPixelPosOnRenderViewport[ 1 ] );

            ImGui::Text( "Cursor Pos (Render Viewport): %d %d", m_CursorPixelPosOnRenderViewport[ 0 ], m_CursorPixelPosOnRenderViewport[ 1 ] );
            ImGui::Text( "Cursor Pos (Film): %d %d", m_CursorPixelPosOnFilm[ 0 ], m_CursorPixelPosOnFilm[ 1 ] );
        }

        ImGui::End();
    }

    if ( m_ShowRayTracingUI && m_Scene.m_HasValidScene )
    {
        ImGui::Begin( "Ray Tracing Tool" );

        ImGui::InputInt2( "Pixel Position", (int*)m_RayTracingPixelPos );
        ImGui::DragFloat2( "Sub-pixel Position", (float*)m_RayTracingSubPixelPos, .1f, 0.f, .999999f, "%.6f", ImGuiSliderFlags_AlwaysClamp );
        if ( ImGui::Button( "Trace" ) )
        {
            DirectX::XMFLOAT2 screenPos = { (float)m_RayTracingPixelPos[ 0 ] + m_RayTracingSubPixelPos[ 0 ], (float)m_RayTracingPixelPos[ 1 ] + m_RayTracingSubPixelPos[ 1 ] };
            screenPos.x /= m_Scene.m_ResolutionWidth;
            screenPos.y /= m_Scene.m_ResolutionHeight;

            XMVECTOR rayOrigin, rayDirection;
            m_Scene.ScreenToCameraRay( screenPos, &rayOrigin, &rayDirection );
            m_RayTracingHasHit = m_Scene.TraceRay( rayOrigin, rayDirection, 0.f, &m_RayTracingHit );
        }

        if ( m_RayTracingHasHit )
        {
            SRayHit* hit = &m_RayTracingHit;
            char stringBuffer[ 512 ];
            sprintf_s( stringBuffer, ARRAY_LENGTH( stringBuffer ), "Found hit\nDistance: %f\nCoord: %f %f\nInstance: %d\nMesh index: %d\nMesh: %s\nTriangle: %d"
                , hit->m_T, hit->m_U, hit->m_V, hit->m_InstanceIndex, hit->m_MeshIndex, m_Scene.m_Meshes[ hit->m_MeshIndex ].GetName().c_str(), hit->m_TriangleIndex );
            ImGui::InputTextMultiline( "Result", stringBuffer, ARRAY_LENGTH( stringBuffer ), ImVec2( 0, 0 ), ImGuiInputTextFlags_ReadOnly );
        }
        else
        {
            char stringBuffer[] = "No hit";
            ImGui::InputText( "Result", stringBuffer, ARRAY_LENGTH( stringBuffer ), ImGuiInputTextFlags_ReadOnly );
        }

        ImGui::End();
    }

    {
        if ( !CMessagebox::GetSingleton().IsEmpty() )
        {
            CMessagebox::GetSingleton().OnImGUI();
        }
    }
}
