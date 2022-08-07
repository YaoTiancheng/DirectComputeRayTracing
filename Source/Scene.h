#pragma once

#include "Camera.h"
#include "Mesh.h"
#include "../Shaders/Material.inc.hlsl"

enum class ELightType
{
    Point = 0,
    Rectangle = 1,
};

enum class EFilter
{
    Box = 0,
    Triangle = 1,
    Gaussian = 2,
    Mitchell = 3,
    LanczosSinc = 4,
};

struct SLight
{
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 rotation;
    DirectX::XMFLOAT3 color;
    DirectX::XMFLOAT2 size;
    ELightType lightType;
};

struct STriangleLight
{
    uint32_t m_TriangleId;
    DirectX::XMFLOAT3 m_Radiance;
    float m_InvSurfaceArea;
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

    int m_LightSelectionIndex = -1;
    int m_MaterialSelectionIndex = -1;
    bool m_IsCameraSelected = false;
};

class CScene
{
public:
    bool LoadFromFile( const char* filepath );

    void Reset();

    bool LoadEnvironmentTextureFromFile( const wchar_t* filepath );

    void UpdateLightGPUData();

    void UpdateMaterialGPUData();

    float GetFilmDistance() const;

    uint32_t GetLightCount() const { return (uint32_t)( m_Lights.size() + m_TriangleLights.size() ); }

    float CalculateFocalDistance() const;

    float CalculateFilmDistance() const;

    float CalculateFilmDistanceNormalized() const;

    float CalculateApertureDiameter() const { return m_FocalLength / m_RelativeAperture; }

    const uint32_t s_MaxRayBounce = 20;
    const uint32_t s_MaxLightsCount = 64;
    const float s_MaxFocalDistance = 999999.0f;

private:
    bool LoadFromWavefrontOBJFile( const char* filepath );

    bool LoadFromXMLFile( const std::filesystem::path& filepath );

public:
    std::string m_EnvironmentImageFilepath;

    DirectX::XMFLOAT2 m_FilmSize;
    float m_FilmDistanceNormalized;
    float m_FocalLength;
    float m_FocalDistance;
    float m_RelativeAperture;
    uint32_t m_ApertureBladeCount;
    float m_ApertureRotation;
    float m_ShutterTime;
    float m_ISO;
    bool m_IsManualFilmDistanceEnabled = false;
    DirectX::XMFLOAT4 m_BackgroundColor;
    uint32_t m_MaxBounceCount;
    float m_FilterRadius = 1.0f;
    EFilter m_Filter = EFilter::Box;
    float m_GaussianFilterAlpha = 1.5f;
    float m_MitchellB = 1.f / 3.f;
    float m_MitchellC = 1.f / 3.f;
    uint32_t m_LanczosSincTau = 3;

    bool m_HasValidScene = false;
    bool m_IsBVHDisabled;
    bool m_IsGGXVNDFSamplingEnabled = true;

    Camera m_Camera;
    std::vector<SLight> m_Lights;
    std::vector<STriangleLight> m_TriangleLights;
    Mesh m_Mesh;

    GPUTexturePtr m_EnvironmentTexture;
    GPUBufferPtr m_VerticesBuffer;
    GPUBufferPtr m_TrianglesBuffer;
    GPUBufferPtr m_BVHNodesBuffer;
    GPUBufferPtr m_LightsBuffer;
    GPUBufferPtr m_MaterialIdsBuffer;
    GPUBufferPtr m_MaterialsBuffer;

    SSceneObjectSelection m_ObjectSelection;
};