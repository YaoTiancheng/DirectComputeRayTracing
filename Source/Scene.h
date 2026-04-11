#pragma once

#include "D3D12Resource.h"
#include "BVHAccel.h"
#include "Camera.h"
#include "Mesh.h"
#include "Texture.h"
#include "Material.h"
#include "BxDFTextures.h"
#include "../Shaders/Material.inc.hlsl"

#define INDEX_NONE -1

enum class ECameraType
{
    PinHole = 0,
    ThinLens = 1,
};

enum class EFilter
{
    Box = 0,
    Triangle = 1,
    Gaussian = 2,
    Mitchell = 3,
    LanczosSinc = 4,
};

struct SPunctualLight
{
    DirectX::XMFLOAT3 m_Position;
    DirectX::XMFLOAT3 m_EulerAngles;
    DirectX::XMFLOAT3 m_Color;
    bool m_IsDirectionalLight;

    void SetEulerAnglesFromDirection( const DirectX::XMFLOAT3& direction );
    DirectX::XMFLOAT3 CalculateDirection() const;
};

struct SMeshLight
{
    uint32_t m_InstanceIndex;
    DirectX::XMFLOAT3 color;
};

struct SEnvironmentLight
{
    DirectX::XMFLOAT3 m_Color;
    CD3D12ResourcePtr<GPUTexture> m_Texture;
    std::string m_TextureFileName;

    bool CreateTextureFromFile();
};

struct SMeshInstance
{
    uint32_t m_MeshIndex;
    uint32_t m_MaterialIdOverride;
};

struct SMeshFlags
{
    uint8_t m_Opaque : 1;
};

struct SRayHit
{
    float m_T;
    float m_U;
    float m_V;
    uint32_t m_InstanceIndex;
    uint32_t m_MeshIndex;
    uint32_t m_TriangleIndex;
};

struct SRayTraversalCounters
{
    uint32_t m_TriangleTestsCount;
    uint32_t m_BoundingBoxTestsCount;
    uint32_t m_BLASEnteringsCount;
    uint32_t m_BLASLeafTestsCount;
};

struct SSceneObjectSelection
{
    void SelectPunctualLight( int index )
    {
        DeselectAll();
        m_PunctualLightSelectionIndex = index;
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

    void SelectEnvironmentLight()
    {
        DeselectAll();
        m_IsEnvironmentLightSelected = true;
    }

    void DeselectAll()
    {
        m_PunctualLightSelectionIndex = -1;
        m_MaterialSelectionIndex = -1;
        m_IsCameraSelected = false;
        m_IsEnvironmentLightSelected = false;
    }

    int m_PunctualLightSelectionIndex = -1;
    int m_MaterialSelectionIndex = -1;
    bool m_IsCameraSelected = false;
    bool m_IsEnvironmentLightSelected = false;
};

class CScene
{
public:
    bool LoadFromFile( const std::filesystem::path& filepath );

    void Reset();

    bool RecreateFilmTextures();

    void UpdateLightGPUData();

    void UpdateMaterialGPUData();

    void UpdateInstanceFlagsGPUData();

    void SetMeshFlagsDirty() { m_IsMeshFlagsDirty = true; }

    void RebuildMeshFlagsIfDirty();

    uint32_t GetLightCount() const { return (uint32_t)m_MeshLights.size() + (uint32_t)m_PunctualLights.size() + ( m_EnvironmentLight ? 1 : 0 ); }

    float CalculateFilmDistance() const;

    float CalculateApertureDiameter() const;

    bool XM_CALLCONV TraceRay( DirectX::FXMVECTOR origin, DirectX::FXMVECTOR direction, float tMin, SRayHit* outRayHit, SRayTraversalCounters* outCounters = nullptr ) const;

    void ScreenToCameraRay( const DirectX::XMFLOAT2& screenPos, DirectX::XMVECTOR* origin, DirectX::XMVECTOR* direction );

    D3D12_GPU_DESCRIPTOR_HANDLE GetTextureDescriptorTable() const { return m_TextureDescriptorTable; }

    void AllocateAndUpdateTextureDescriptorTable();

    bool InitSceneLuminance();

    bool ResizeSceneLuminanceInputResolution( uint32_t resolutionWidth, uint32_t resolutionHeight );

    bool InitSampleConvolution();

    bool InitPostProcessing();

    const uint32_t s_MaxRayBounce = 20;
    const uint32_t s_MaxLightsCount = 5000;
    const float s_MaxFocalDistance = 999999.0f;

private:
    bool LoadFromWavefrontOBJFile( const std::filesystem::path& filename );

    bool LoadFromXMLFile( const std::filesystem::path& filepath );

public:
    uint32_t m_ResolutionWidth;
    uint32_t m_ResolutionHeight;
    DirectX::XMFLOAT2 m_FilmSize;
    ECameraType m_CameraType;
    float m_FoVX;
    float m_FocalLength;
    float m_FocalDistance;
    float m_RelativeAperture;
    uint32_t m_ApertureBladeCount;
    float m_ApertureRotation;
    float m_ShutterTime;
    float m_ISO;
    uint32_t m_MaxBounceCount;
    float m_FilterRadius = 1.0f;
    EFilter m_Filter = EFilter::Box;
    float m_GaussianFilterAlpha = 1.5f;
    float m_MitchellB = 1.f / 3.f;
    float m_MitchellC = 1.f / 3.f;
    uint32_t m_LanczosSincTau = 3;

    uint32_t m_FrameSeed = 0;

    bool m_HasValidScene = false;
    bool m_TraverseBVHFrontToBack = true;
    bool m_IsGGXVNDFSamplingEnabled = true;
    bool m_IsLightVisible = true;
    bool m_WatertightRayTriangleIntersection = true;
    bool m_AllowAnyHitShader = false;

    Camera m_Camera;
    std::shared_ptr<SEnvironmentLight> m_EnvironmentLight;
    std::vector<SPunctualLight> m_PunctualLights;
    std::vector<SMeshLight> m_MeshLights;
    std::vector<SMaterial> m_Materials;
    std::vector<Mesh> m_Meshes;
    std::vector<SMeshFlags> m_MeshFlags;
    std::vector<SMeshInstance> m_MeshInstances;
    std::vector<BVHAccel::BVHNode> m_TLAS;
    std::vector<uint32_t> m_OriginalInstanceIndices; // Original indices indexed by reordered index
    std::vector<uint32_t> m_ReorderedInstanceIndices; // Reordered indices indexed by original index
    std::vector<DirectX::XMFLOAT4X3> m_InstanceTransforms;
    std::vector<CTexture> m_Textures;
    uint32_t m_BVHTraversalStackSize;

    CD3D12ResourcePtr<GPUBuffer> m_VerticesBuffer;
    CD3D12ResourcePtr<GPUBuffer> m_TrianglesBuffer;
    CD3D12ResourcePtr<GPUBuffer> m_BVHNodesBuffer;
    CD3D12ResourcePtr<GPUBuffer> m_LightsBuffer;
    CD3D12ResourcePtr<GPUBuffer> m_MaterialIdsBuffer;
    CD3D12ResourcePtr<GPUBuffer> m_MaterialsBuffer;
    CD3D12ResourcePtr<GPUBuffer> m_InstanceTransformsBuffer;
    CD3D12ResourcePtr<GPUBuffer> m_InstanceFlagsBuffer;
    CD3D12ResourcePtr<GPUBuffer> m_InstanceMaterialOverrideBuffer;
    CD3D12ResourcePtr<GPUBuffer> m_InstanceLightIndicesBuffer;
    std::vector<CD3D12ResourcePtr<GPUTexture>> m_GPUTextures;

    CD3D12ResourcePtr<GPUTexture> m_FilmTexture;
    CD3D12ResourcePtr<GPUTexture> m_SamplePositionTexture;
    CD3D12ResourcePtr<GPUTexture> m_SampleValueTexture;
    CD3D12ResourcePtr<GPUTexture> m_RenderResultTexture;

    D3D12_GPU_DESCRIPTOR_HANDLE m_TextureDescriptorTable;

    float m_LuminanceWhite = 1.f;
    float m_ManualEV100 = 15.f;
    bool m_IsPostFXEnabled = true;
    bool m_IsAutoExposureEnabled = true;
    bool m_CalculateEV100FromCamera = true;

    int32_t m_SampleConvolutionFilterIndex = -1;
    ComPtr<ID3D12RootSignature> m_SampleConvolutionRootSignature;
    std::shared_ptr<ID3D12PipelineState> m_SampleConvolutionPSO;

    ComPtr<ID3D12RootSignature> m_SceneLuminanceRootSignature;
    ComPtr<ID3D12PipelineState> m_SumLuminanceTo1DPSO;
    ComPtr<ID3D12PipelineState> m_SumLuminanceToSinglePSO;

    CD3D12ResourcePtr<GPUBuffer> m_SumLuminanceBuffer0;
    CD3D12ResourcePtr<GPUBuffer> m_SumLuminanceBuffer1;
    GPUBuffer* m_LuminanceResultBuffer = nullptr;

    GPUBufferPtr m_ScreenQuadVerticesBuffer;

    ComPtr<ID3D12RootSignature> m_PostProcessingRootSignature;
    ComPtr<ID3D12PipelineState> m_PostFXPSO;
    ComPtr<ID3D12PipelineState> m_PostFXAutoExposurePSO;
    ComPtr<ID3D12PipelineState> m_PostFXDisabledPSO;
    ComPtr<ID3D12PipelineState> m_CopyPSO;

    SBxDFTextures m_BxDFTextures;

    class CPathTracer* m_PathTracer[ 2 ] = { nullptr, nullptr };

    bool m_IsMeshFlagsDirty = false;
    bool m_IsLightGPUBufferDirty = false;
    bool m_IsMaterialGPUBufferDirty = false;
    bool m_IsInstanceFlagsBufferDirty = false;
    bool m_IsFilmDirty = true;
    bool m_IsLastFrameFilmDirty = true;

    // Resource states
    D3D12_RESOURCE_STATES m_FilmTextureStates;
    bool m_IsLightBufferRead = true;
    bool m_IsMaterialBufferRead = true;
    bool m_IsInstanceFlagsBufferRead = true;
    bool m_IsSampleTexturesRead = false;
    bool m_IsRenderResultTextureRead = true;

    SSceneObjectSelection m_ObjectSelection;
};