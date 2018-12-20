#pragma once

struct Sphere
{
    DirectX::XMFLOAT4   center;
    float               radius;
    DirectX::XMFLOAT4   albedo;
};


struct RayTracingConstants
{
    uint32_t            maxBounceCount;
    uint32_t            sphereCount;
    uint32_t            samplesCount;
    uint32_t            samplesCountPerPixel;
    DirectX::XMFLOAT2   resolution;
    DirectX::XMFLOAT2   filmSize;
    float               filmDistance;
    DirectX::XMMATRIX   cameraTransform;
    DirectX::XMFLOAT4   background;
};


class Scene
{
public:

    bool Init(uint32_t resolutionWidth, uint32_t resolutionHeight);

private:
    static const int kMaxSamplesCount = 65536;
    static const int kMaxSpheresCount = 32;

    RayTracingConstants                 m_RayTracingConstants;
    float                               m_Samples[kMaxSamplesCount];
    Sphere                              m_Spheres[kMaxSpheresCount];

    template <typename T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;
    ComPtr<ID3D11ComputeShader>         m_RayTracingComputeShader;
    ComPtr<ID3D11ShaderResourceView>    m_RayTracingConstantsSRV;
    ComPtr<ID3D11ShaderResourceView>    m_SamplesSRV;
    ComPtr<ID3D11ShaderResourceView>    m_SpheresSRV;
    ComPtr<ID3D11ShaderResourceView>    m_FilmTextureSRV;
    ComPtr<ID3D11UnorderedAccessView>   m_FilmTextureUAV;
    ComPtr<ID3D11Texture2D>             m_FilmTexture;
    ComPtr<ID3D11Buffer>                m_RayTracingConstantsBuffer;
    ComPtr<ID3D11Buffer>                m_SamplesBuffer;
    ComPtr<ID3D11Buffer>                m_SpheresBuffer;
};