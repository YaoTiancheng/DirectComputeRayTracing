#pragma once

enum class EMaterialType
{
    Diffuse = 0,
    Plastic = 1,
    Conductor = 2,
    Dielectric = 3,
    ThinDielectric = 4,
};

struct SMaterial
{
    DirectX::XMFLOAT3 m_Albedo;
    float m_Roughness;
    DirectX::XMFLOAT3 m_IOR;
    float m_Opacity;
    DirectX::XMFLOAT3 m_K;
    DirectX::XMFLOAT2 m_Tiling;
    std::string m_Name;
    EMaterialType m_MaterialType;
    int32_t m_AlbedoTextureIndex;
    bool m_Multiscattering;
    bool m_IsTwoSided;
    bool m_HasRoughnessTexture;
};