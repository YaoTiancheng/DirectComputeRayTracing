#pragma once

#include "Camera.h"
#include "../Shaders/Material.inc.hlsl"

enum class ELightType
{
    Point = 0,
    Rectangle = 1,
};

struct SLightSetting
{
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 rotation;
    DirectX::XMFLOAT3 color;
    DirectX::XMFLOAT2 size;
    ELightType lightType;
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

    bool LoadEnvironmentTextureFromFile( const wchar_t* filepath );

    void UpdateLightGPUData();

    void UpdateMaterialGPUData();

    float GetFilmDistance() const;

    float CalculateFocalDistance() const;

    float CalculateFilmDistance() const;

    float CalculateFilmDistanceNormalized() const;

    const uint32_t s_MaxRayBounce = 20;
    const uint32_t s_MaxLightsCount = 64;
    const float s_MaxFocalDistance = 999999.0f;

    std::string m_EnvironmentImageFilepath;

    DirectX::XMFLOAT2 m_FilmSize;
    float m_FilmDistanceNormalized;
    float m_FocalLength;
    float m_FocalDistance;
    float m_ApertureDiameter;
    uint32_t m_ApertureBladeCount;
    float m_ApertureRotation;
    bool m_IsManualFilmDistanceEnabled = false;
    DirectX::XMFLOAT4 m_BackgroundColor;
    uint32_t m_MaxBounceCount;
    uint32_t m_PrimitiveCount;

    bool m_HasValidScene = false;
    bool m_IsBVHDisabled;
    bool m_IsGGXVNDFSamplingEnabled = true;
    uint32_t m_BVHTraversalStackSize;

    Camera m_Camera;
    std::vector<SLightSetting> m_LightSettings;
    std::vector<Material> m_Materials;
    std::vector<std::string> m_MaterialNames;

    GPUTexturePtr m_EnvironmentTexture;
    GPUBufferPtr m_VerticesBuffer;
    GPUBufferPtr m_TrianglesBuffer;
    GPUBufferPtr m_BVHNodesBuffer;
    GPUBufferPtr m_LightsBuffer;
    GPUBufferPtr m_MaterialIdsBuffer;
    GPUBufferPtr m_MaterialsBuffer;

    SSceneObjectSelection m_ObjectSelection;
};