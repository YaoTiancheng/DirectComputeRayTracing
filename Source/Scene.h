#pragma once

#include "Primitive.h"
#include "Camera.h"
#include "BVHAccel.h"
#include "GPUBuffer.h"

struct PointLight
{
    DirectX::XMFLOAT3   position;
    DirectX::XMFLOAT3   color;
};

struct RayTracingConstants
{
    uint32_t            maxBounceCount;
    uint32_t            primitiveCount;
    uint32_t            pointLightCount;
    uint32_t            samplesCount;
    uint32_t            resolutionX;
    uint32_t            resolutionY;
    DirectX::XMFLOAT2   filmSize;
    float               filmDistance;
    uint32_t            padding[ 3 ];
    DirectX::XMFLOAT4X4 cameraTransform;
    DirectX::XMFLOAT4   background;
};


class Scene
{
public:
    Scene();

    bool Init();

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
    static const int kMaxBVHNodeCount = 256;
    static const int kMaxVertexIndexCount = kMaxTriangleCount * 3;


    Camera                              m_Camera;

    D3D11_VIEWPORT                      m_DefaultViewport;

    RayTracingConstants                 m_RayTracingConstants;
    Vertex                              m_Vertices[ kMaxVertexCount ];
    uint32_t                            m_Triangles[ kMaxVertexIndexCount ];
    PackedBVHNode                       m_BVHNodes[ kMaxBVHNodeCount ];
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
    
    ComPtr<ID3D11InputLayout>           m_ScreenQuadVertexInputLayout;
    ComPtr<ID3D11RenderTargetView>      m_DefaultRenderTargetView;
    ComPtr<ID3D11RenderTargetView>      m_FilmTextureRenderTargetView;

    using GPUBufferPtr = std::unique_ptr<GPUBuffer>;
    GPUBufferPtr                        m_RayTracingConstantsBuffer;
    GPUBufferPtr                        m_CookTorranceCompTextureConstantsBuffer;
    GPUBufferPtr                        m_SamplesBuffer;
    GPUBufferPtr                        m_VerticesBuffer;
    GPUBufferPtr                        m_TrianglesBuffer;
    GPUBufferPtr                        m_BVHNodesBuffer;
    GPUBufferPtr                        m_PointLightsBuffer;
    GPUBufferPtr                        m_ScreenQuadVerticesBuffer;
};