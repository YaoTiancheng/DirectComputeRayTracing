#pragma once

#include "Camera.h"

struct Sphere
{
    DirectX::XMFLOAT4   center;
    float               radius;
    DirectX::XMFLOAT4   albedo;
    float               metallic;
    DirectX::XMFLOAT4   emission;
};


struct Vertex
{
    DirectX::XMFLOAT4   position;
    DirectX::XMFLOAT4   normal;
    DirectX::XMFLOAT4   tangent;
};


struct PointLight
{
    DirectX::XMFLOAT4   position;
    DirectX::XMFLOAT4   color;
};


struct RayTracingConstants
{
    uint32_t            maxBounceCount;
    uint32_t            primitiveCount;
    uint32_t            pointLightCount;
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

    bool Init( uint32_t resolutionWidth, uint32_t resolutionHeight );

    void ResetScene();

    void AddOneSampleAndRender();

    bool OnWndMessage( UINT message, WPARAM wParam, LPARAM lParam );

private:
    void AddOneSample();

    void DoPostProcessing();

    bool UpdateResources();

    void DispatchRayTracing();

    void ClearFilmTexture();


private:
    static const int kMaxSamplesCount = 65536;
    static const int kMaxPointLightsCount = 8;
    static const int kMaxVertexCount = 256;
    static const int kMaxTriangleCount = 256;
    static const int kMaxVertexIndexCount = kMaxTriangleCount * 3;


    Camera                              m_Camera;

    D3D11_VIEWPORT                      m_DefaultViewport;

    RayTracingConstants                 m_RayTracingConstants;
    Vertex                              m_Vertices[ kMaxVertexCount ];
    uint32_t                            m_Triangles[ kMaxVertexIndexCount ];
    PointLight                          m_PointLights[ kMaxPointLightsCount ];

    bool                                m_IsFilmDirty;

    std::mt19937                        m_MersenneURBG;
    std::uniform_real_distribution<float>   m_UniformRealDistribution;

    template <typename T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;
    ComPtr<ID3D11SamplerState>          m_CopySamplerState;
    ComPtr<ID3D11SamplerState>          m_UVClampSamplerState;
    ComPtr<ID3D11ComputeShader>         m_RayTracingComputeShader;
    ComPtr<ID3D11VertexShader>          m_ScreenQuadVertexShader;
    ComPtr<ID3D11PixelShader>           m_CopyPixelShader;
    ComPtr<ID3D11ShaderResourceView>    m_RayTracingConstantsSRV;
    ComPtr<ID3D11ShaderResourceView>    m_SamplesSRV;
    ComPtr<ID3D11ShaderResourceView>    m_VerticesSRV;
    ComPtr<ID3D11ShaderResourceView>    m_TrianglesSRV;
    ComPtr<ID3D11ShaderResourceView>    m_PointLightsSRV;
    ComPtr<ID3D11ShaderResourceView>    m_FilmTextureSRV;
    ComPtr<ID3D11UnorderedAccessView>   m_FilmTextureUAV;
    ComPtr<ID3D11Texture2D>             m_FilmTexture;
    ComPtr<ID3D11Texture2D>             m_CookTorranceCompETexture;
    ComPtr<ID3D11ShaderResourceView>    m_CookTorranceCompETextureSRV;
    ComPtr<ID3D11Texture2D>             m_CookTorranceCompEAvgTexture;
    ComPtr<ID3D11ShaderResourceView>    m_CookTorranceCompEAvgTextureSRV;
    ComPtr<ID3D11Texture2D>             m_CookTorranceCompInvCDFTexture;
    ComPtr<ID3D11ShaderResourceView>    m_CookTorranceCompInvCDFTextureSRV;
    ComPtr<ID3D11Texture2D>             m_CookTorranceCompPdfScaleTexture;
    ComPtr<ID3D11ShaderResourceView>    m_CookTorranceCompPdfScaleTextureSRV;
    ComPtr<ID3D11Texture2D>             m_CookTorranceCompEFresnelTexture;
    ComPtr<ID3D11ShaderResourceView>    m_CookTorranceCompEFresnelTextureSRV;
    ComPtr<ID3D11Buffer>                m_RayTracingConstantsBuffer;
    ComPtr<ID3D11Buffer>                m_CookTorranceCompTextureConstantsBuffer;
    ComPtr<ID3D11Buffer>                m_SamplesBuffer;
    ComPtr<ID3D11Buffer>                m_VerticesBuffer;
    ComPtr<ID3D11Buffer>                m_TrianglesBuffer;
    ComPtr<ID3D11Buffer>                m_PointLightBuffer;
    ComPtr<ID3D11Buffer>                m_ScreenQuadVertexBuffer;
    ComPtr<ID3D11InputLayout>           m_ScreenQuadVertexInputLayout;
    ComPtr<ID3D11RenderTargetView>      m_DefaultRenderTargetView;
    ComPtr<ID3D11RenderTargetView>      m_FilmTextureRenderTargetView;
};