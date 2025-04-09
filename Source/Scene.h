#pragma once

#include "D3D12Resource.h"
#include "BVHAccel.h"
#include "Camera.h"
#include "Mesh.h"
#include "Texture.h"
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

enum class EMaterialType
{
    Diffuse = 0,
    Plastic = 1,
    Conductor = 2,
    Dielectric = 3
};

struct SMaterial
{
    DirectX::XMFLOAT3 m_Albedo;
    float m_Roughness;
    DirectX::XMFLOAT3 m_IOR;
    DirectX::XMFLOAT3 m_K;
    DirectX::XMFLOAT2 m_Tiling;
    EMaterialType m_MaterialType;
    int32_t m_AlbedoTextureIndex;
    bool m_Multiscattering;
    bool m_IsTwoSided;
    bool m_HasRoughnessTexture;
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

    float GetFilmDistance() const;

    uint32_t GetLightCount() const { return (uint32_t)m_MeshLights.size() + (uint32_t)m_PunctualLights.size() + ( m_EnvironmentLight ? 1 : 0 ); }

    float CalculateFilmDistance() const;

    float CalculateApertureDiameter() const;

    bool XM_CALLCONV TraceRay( DirectX::FXMVECTOR origin, DirectX::FXMVECTOR direction, float tMin, SRayHit* outRayHit ) const;

    void ScreenToCameraRay( const DirectX::XMFLOAT2& screenPos, DirectX::XMVECTOR* origin, DirectX::XMVECTOR* direction );

    D3D12_GPU_DESCRIPTOR_HANDLE GetTextureDescriptorTable() const { return m_TextureDescriptorTable; }

    void AllocateAndUpdateTextureDescriptorTable();

    const uint32_t s_MaxRayBounce = 20;
    const uint32_t s_MaxLightsCount = 5000;
    const float s_MaxFocalDistance = 999999.0f;

private:
    bool LoadFromWavefrontOBJFile( const std::filesystem::path& filepath );

    bool LoadFromXMLFile( const std::filesystem::path& filepath );

    bool CreateMeshAndMaterialsFromWavefrontOBJFile( const char* filename, const char* MTLBaseDir, const SMeshProcessingParams& processingParams );

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

    bool m_HasValidScene = false;
    bool m_TraverseBVHFrontToBack = true;
    bool m_IsGGXVNDFSamplingEnabled = true;
    bool m_IsLightVisible = true;

    Camera m_Camera;
    std::shared_ptr<SEnvironmentLight> m_EnvironmentLight;
    std::vector<SPunctualLight> m_PunctualLights;
    std::vector<SMeshLight> m_MeshLights;
    std::vector<SMaterial> m_Materials;
    std::vector<std::string> m_MaterialNames;
    std::vector<Mesh> m_Meshes;
    std::vector<BVHAccel::BVHNode> m_TLAS;
    std::vector<uint32_t> m_ReorderedInstanceIndices;
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
    CD3D12ResourcePtr<GPUBuffer> m_InstanceLightIndicesBuffer;
    std::vector<CD3D12ResourcePtr<GPUTexture>> m_GPUTextures;

    CD3D12ResourcePtr<GPUTexture> m_FilmTexture;
    CD3D12ResourcePtr<GPUTexture> m_SamplePositionTexture;
    CD3D12ResourcePtr<GPUTexture> m_SampleValueTexture;
    CD3D12ResourcePtr<GPUTexture> m_RenderResultTexture;

    D3D12_GPU_DESCRIPTOR_HANDLE m_TextureDescriptorTable;

    // Resource states
    D3D12_RESOURCE_STATES m_FilmTextureStates;
    bool m_IsLightBufferRead = true;
    bool m_IsMaterialBufferRead = true;
    bool m_IsSampleTexturesRead = false;
    bool m_IsRenderResultTextureRead = true;

    SSceneObjectSelection m_ObjectSelection;
};