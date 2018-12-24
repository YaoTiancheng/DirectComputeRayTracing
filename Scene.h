#pragma once

#include "Camera.h"

struct Sphere
{
    DirectX::XMFLOAT4   center;
    float               radius;
    DirectX::XMFLOAT4   albedo;
    DirectX::XMFLOAT4   emission;
};


struct RayTracingConstants
{
    uint32_t            maxBounceCount;
    uint32_t            sphereCount;
    uint32_t            samplesCount;
    DirectX::XMFLOAT2   resolution;
    DirectX::XMFLOAT2   filmSize;
    float               filmDistance;
    DirectX::XMFLOAT4X4 cameraTransform;
    DirectX::XMFLOAT4   background;
};


class Scene
{
public:
    Scene();

    bool Init(uint32_t resolutionWidth, uint32_t resolutionHeight);

    void ResetScene();

    void AddOneSampleAndRender();

    bool OnWndMessage(UINT message, WPARAM wParam, LPARAM lParam);

private:
    void AddOneSample();

    void DoPostProcessing();

    bool UpdateResources();

    void DispatchRayTracing();

    void ClearFilmTexture();


private:
    static const int kMaxSamplesCount = 65536;
    static const int kMaxSpheresCount = 32;

    Camera                              m_Camera;

    D3D11_VIEWPORT                      m_DefaultViewport;

    RayTracingConstants                 m_RayTracingConstants;
    Sphere                              m_Spheres[kMaxSpheresCount];

    bool                                m_IsFilmDirty;

    template <typename T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;
    ComPtr<ID3D11SamplerState>          m_CopySamplerState;
    ComPtr<ID3D11ComputeShader>         m_RayTracingComputeShader;
    ComPtr<ID3D11VertexShader>          m_ScreenQuadVertexShader;
    ComPtr<ID3D11PixelShader>           m_CopyPixelShader;
    ComPtr<ID3D11ShaderResourceView>    m_RayTracingConstantsSRV;
    ComPtr<ID3D11ShaderResourceView>    m_SamplesSRV;
    ComPtr<ID3D11ShaderResourceView>    m_SpheresSRV;
    ComPtr<ID3D11ShaderResourceView>    m_FilmTextureSRV;
    ComPtr<ID3D11UnorderedAccessView>   m_FilmTextureUAV;
    ComPtr<ID3D11Texture2D>             m_FilmTexture;
    ComPtr<ID3D11Buffer>                m_RayTracingConstantsBuffer;
    ComPtr<ID3D11Buffer>                m_SamplesBuffer;
    ComPtr<ID3D11Buffer>                m_SpheresBuffer;
    ComPtr<ID3D11Buffer>                m_ScreenQuadVertexBuffer;
    ComPtr<ID3D11InputLayout>           m_ScreenQuadVertexInputLayout;
    ComPtr<ID3D11RenderTargetView>      m_DefaultRenderTargetView;
    ComPtr<ID3D11RenderTargetView>      m_FilmTextureRenderTargetView;
};