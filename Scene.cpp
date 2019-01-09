#include "stdafx.h"
#include "Scene.h"
#include "D3D11RenderSystem.h"

using namespace DirectX;

XMFLOAT4 kScreenQuadVertices[6] = 
{
    { -1.0f,  1.0f,  0.0f,  1.0f },
    {  1.0f,  1.0f,  0.0f,  1.0f },
    { -1.0f, -1.0f,  0.0f,  1.0f },
    { -1.0f, -1.0f,  0.0f,  1.0f },
    {  1.0f,  1.0f,  0.0f,  1.0f },
    {  1.0f, -1.0f,  0.0f,  1.0f },
};

D3D11_INPUT_ELEMENT_DESC kScreenQuadInputElementDesc[1]
{
    { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
};

Scene::Scene()
    : m_IsFilmDirty(true)
    , m_UniformRealDistribution(0.0f, 1.0f)
{
    std::random_device randomDevice;
    m_MersenneURBG = std::mt19937(randomDevice());
}

bool Scene::Init(uint32_t resolutionWidth, uint32_t resolutionHeight)
{
    ID3D11Device* device = GetDevice();

    D3D11_TEXTURE2D_DESC textureDesc;
    ZeroMemory(&textureDesc, sizeof(D3D11_TEXTURE2D_DESC));
    textureDesc.Width               = resolutionWidth;
    textureDesc.Height              = resolutionHeight;
    textureDesc.MipLevels           = 1;
    textureDesc.ArraySize           = 1;
    textureDesc.Format              = DXGI_FORMAT_R32G32B32A32_FLOAT;
    textureDesc.SampleDesc.Count    = 1;
    textureDesc.Usage               = D3D11_USAGE_DEFAULT;
    textureDesc.BindFlags           = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET;
    HRESULT hr = device->CreateTexture2D(&textureDesc, nullptr, &m_FilmTexture);
    if (FAILED(hr))
        return false;

    hr = device->CreateUnorderedAccessView(m_FilmTexture.Get(), nullptr, &m_FilmTextureUAV);
    if (FAILED(hr))
        return false;

    hr = device->CreateShaderResourceView(m_FilmTexture.Get(), nullptr, &m_FilmTextureSRV);
    if (FAILED(hr))
        return false;

    ID3DBlob* shaderBlob = CompileFromFile(L"RayTracing.hlsl", "main", "cs_5_0");
    if (!shaderBlob)
        return false;

    m_RayTracingComputeShader.Attach(CreateComputeShader(shaderBlob));
    shaderBlob->Release();
    if (!m_RayTracingComputeShader)
        return false;

    D3D11_BUFFER_DESC bufferDesc;
    ZeroMemory(&bufferDesc, sizeof(D3D11_BUFFER_DESC));
    bufferDesc.ByteWidth = sizeof(RayTracingConstants);
    bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bufferDesc.StructureByteStride = sizeof(RayTracingConstants);
    bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    hr = device->CreateBuffer(&bufferDesc, nullptr, &m_RayTracingConstantsBuffer);
    if (FAILED(hr))
        return false;

    bufferDesc.ByteWidth = kMaxSamplesCount * sizeof(float);
    bufferDesc.StructureByteStride = sizeof(float);
    hr = device->CreateBuffer(&bufferDesc, nullptr, &m_SamplesBuffer);
    if (FAILED(hr))
        return false;

    bufferDesc.ByteWidth = sizeof(m_Spheres);
    bufferDesc.StructureByteStride = sizeof(Sphere);
    hr = device->CreateBuffer(&bufferDesc, nullptr, &m_SpheresBuffer);
    if (FAILED(hr))
        return false;

    bufferDesc.ByteWidth = sizeof(m_PointLights);
    bufferDesc.StructureByteStride = sizeof(PointLight);
    hr = device->CreateBuffer(&bufferDesc, nullptr, &m_PointLightBuffer);
    if (FAILED(hr))
        return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc;
    SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
    SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    SRVDesc.Buffer.ElementOffset = 0;
    SRVDesc.Buffer.NumElements = 1;
    hr = device->CreateShaderResourceView(m_RayTracingConstantsBuffer.Get(), &SRVDesc, &m_RayTracingConstantsSRV);
    if (FAILED(hr))
        return false;

    SRVDesc.Buffer.NumElements = kMaxSamplesCount;
    hr = device->CreateShaderResourceView(m_SamplesBuffer.Get(), &SRVDesc, &m_SamplesSRV);
    if (FAILED(hr))
        return false;

    SRVDesc.Buffer.NumElements = kMaxSpheresCount;
    hr = device->CreateShaderResourceView(m_SpheresBuffer.Get(), &SRVDesc, &m_SpheresSRV);
    if (FAILED(hr))
        return false;

    SRVDesc.Buffer.NumElements = kMaxPointLightsCount;
    hr = device->CreateShaderResourceView(m_PointLightBuffer.Get(), &SRVDesc, &m_PointLightsSRV);
    if (FAILED(hr))
        return false;

    shaderBlob = CompileFromFile(L"PostProcessings.hlsl", "ScreenQuadMainVS", "vs_5_0");
    if (!shaderBlob)
        return false;

    m_ScreenQuadVertexShader.Attach(CreateVertexShader(shaderBlob));
    hr = device->CreateInputLayout(kScreenQuadInputElementDesc, 1, shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), &m_ScreenQuadVertexInputLayout);
    shaderBlob->Release();
    if (!m_ScreenQuadVertexShader || !m_ScreenQuadVertexInputLayout)
        return false;

    shaderBlob = CompileFromFile(L"PostProcessings.hlsl", "CopyMainPS", "ps_5_0");
    if (!shaderBlob)
        return false;

    m_CopyPixelShader.Attach(CreatePixelShader(shaderBlob));
    shaderBlob->Release();
    if (!m_CopyPixelShader)
        return false;

    bufferDesc.ByteWidth = sizeof(kScreenQuadVertices);
    bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
    bufferDesc.CPUAccessFlags = 0;
    bufferDesc.StructureByteStride = sizeof(XMFLOAT4);
    bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bufferDesc.MiscFlags = 0;
    D3D11_SUBRESOURCE_DATA subresourceData;
    subresourceData.SysMemPitch = 0;
    subresourceData.SysMemSlicePitch = 0;
    subresourceData.pSysMem = kScreenQuadVertices;
    hr = device->CreateBuffer(&bufferDesc, &subresourceData, &m_ScreenQuadVertexBuffer);
    if (FAILED(hr))
        return false;

    ID3D11Texture2D *backBuffer = nullptr;
    GetSwapChain()->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)(&backBuffer));
    hr = device->CreateRenderTargetView(backBuffer, nullptr, &m_DefaultRenderTargetView);
    backBuffer->Release();
    if (FAILED(hr))
        return false;

    D3D11_SAMPLER_DESC samplerDesc;
    ZeroMemory(&samplerDesc, sizeof(D3D11_SAMPLER_DESC));
    samplerDesc.Filter = D3D11_FILTER_MAXIMUM_MIN_MAG_MIP_POINT;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MaxAnisotropy = 1;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    hr = device->CreateSamplerState(&samplerDesc, &m_CopySamplerState);
    if (FAILED(hr))
        return false;

    hr = device->CreateRenderTargetView(m_FilmTexture.Get(), nullptr, &m_FilmTextureRenderTargetView);
    if (FAILED(hr))
        return false;

    m_DefaultViewport = { 0.0f, 0.0f, (float)resolutionWidth, (float)resolutionHeight, 0.0f, 1.0f };
    m_RayTracingConstants.samplesCount = kMaxSamplesCount;
    m_RayTracingConstants.resolution = { (float)resolutionWidth, (float)resolutionHeight };

    return true;
}

void Scene::ResetScene()
{
    m_Spheres[0].center = XMFLOAT4(0.0f, 0.5f, 2.6f, 1.0f);
    m_Spheres[0].radius = 0.5f;
    m_Spheres[0].albedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    m_Spheres[0].metallic = 0.0f;
    m_Spheres[0].emission = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);

    m_Spheres[1].center = XMFLOAT4(1.0f, 0.5f, 2.6f, 1.0f);
    m_Spheres[1].radius = 0.5f;
    m_Spheres[1].albedo = XMFLOAT4(0.78f, 0.38f, 0.38f, 1.0f);
    m_Spheres[1].metallic = 1.0f;
    m_Spheres[1].emission = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);

    m_Spheres[2].center = XMFLOAT4(-2.0f, 0.5f, 2.6f, 1.0f);
    m_Spheres[2].radius = 0.5f;
    m_Spheres[2].albedo = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
    m_Spheres[2].metallic = 0.0f;
    m_Spheres[2].emission = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);

    m_Spheres[3].center = XMFLOAT4(-0.5f, 1.0f, 3.6f, 1.0f);
    m_Spheres[3].radius = 1.0f;
    m_Spheres[3].albedo = XMFLOAT4(0.78f, 0.48f, 0.78f, 1.0f);
    m_Spheres[3].metallic = 0.0f;
    m_Spheres[3].emission = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);

    m_Spheres[4].center = XMFLOAT4(0.0f, -100.0f, 0.0f, 1.0f);
    m_Spheres[4].radius = 100.0f;
    m_Spheres[4].albedo = XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f);
    m_Spheres[4].metallic = 0.0f;
    m_Spheres[4].emission = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);

    m_PointLights[0].position = XMFLOAT4(10.0f, 3.0f, -5.0f, 1.0f);
    m_PointLights[0].color = XMFLOAT4(300.0f, 300.0f, 300.0f, 1.0f);

    m_RayTracingConstants.maxBounceCount = 6;
    m_RayTracingConstants.sphereCount = 5;
    m_RayTracingConstants.pointLightCount = 0;
    m_RayTracingConstants.filmSize = XMFLOAT2(0.05333f, 0.03f);
    m_RayTracingConstants.filmDistance = 0.03f;
    m_RayTracingConstants.cameraTransform =
        { 1.0f, 0.0f, 0.0f, 0.0f,
          0.0f, 1.0f, 0.0f, 0.0f,
          0.0f, 0.0f, 1.0f, 0.0f,
          0.0f, 0.0f, 0.0f, 1.0f };
    m_RayTracingConstants.background = { 1.f, 1.f, 1.f, 0.f };

    m_IsFilmDirty = true;
}

void Scene::AddOneSampleAndRender()
{
    AddOneSample();
    DoPostProcessing();
}

bool Scene::OnWndMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    return m_Camera.OnWndMessage(message, wParam, lParam);
}

void Scene::AddOneSample()
{
    m_Camera.Update();

    if (m_Camera.IsDirty() || m_IsFilmDirty)
    {
        ClearFilmTexture();
        m_IsFilmDirty = false;

        if (m_Camera.IsDirty())
            m_Camera.GetTransformMatrixAndClearDirty(&m_RayTracingConstants.cameraTransform);
    }

    if (UpdateResources())
        DispatchRayTracing();
}

void Scene::DoPostProcessing()
{
    ID3D11DeviceContext* deviceContext = GetDeviceContext();

    UINT vertexStride = sizeof(XMFLOAT4);
    UINT vertexOffset = 0;
    ID3D11Buffer* rawScreenQuadVertexBuffer = m_ScreenQuadVertexBuffer.Get();
    deviceContext->IASetVertexBuffers(0, 1, &rawScreenQuadVertexBuffer, &vertexStride, &vertexOffset);
    deviceContext->IASetInputLayout(m_ScreenQuadVertexInputLayout.Get());
    deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ID3D11RenderTargetView* rawDefaultRenderTargetView = m_DefaultRenderTargetView.Get();
    deviceContext->OMSetRenderTargets(1, &rawDefaultRenderTargetView, nullptr);

    deviceContext->RSSetViewports(1, &m_DefaultViewport);

    deviceContext->VSSetShader(m_ScreenQuadVertexShader.Get(), nullptr, 0);
    deviceContext->PSSetShader(m_CopyPixelShader.Get(), nullptr, 0);

    ID3D11SamplerState* rawCopySamplerState = m_CopySamplerState.Get();
    deviceContext->PSSetSamplers(0, 1, &rawCopySamplerState);
    ID3D11ShaderResourceView* rawFilmTextureSRV = m_FilmTextureSRV.Get();
    deviceContext->PSSetShaderResources(0, 1, &rawFilmTextureSRV);

    deviceContext->Draw(6, 0);

    rawFilmTextureSRV = nullptr;
    deviceContext->PSSetShaderResources(0, 1, &rawFilmTextureSRV);
}

bool Scene::UpdateResources()
{
    ID3D11DeviceContext* deviceContext = GetDeviceContext();

    D3D11_MAPPED_SUBRESOURCE mappedSubresource;

    ZeroMemory(&mappedSubresource, sizeof(D3D11_MAPPED_SUBRESOURCE));
    HRESULT hr = deviceContext->Map(m_RayTracingConstantsBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSubresource);
    if (SUCCEEDED(hr))
    {
        memcpy(mappedSubresource.pData, &m_RayTracingConstants, sizeof(m_RayTracingConstants));
        deviceContext->Unmap(m_RayTracingConstantsBuffer.Get(), 0);
    }
    else
    {
        return false;
    }

    ZeroMemory(&mappedSubresource, sizeof(D3D11_MAPPED_SUBRESOURCE));
    hr = deviceContext->Map(m_SpheresBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSubresource);
    if (SUCCEEDED(hr))
    {
        memcpy(mappedSubresource.pData, m_Spheres, sizeof(Sphere) * m_RayTracingConstants.sphereCount);
        deviceContext->Unmap(m_SpheresBuffer.Get(), 0);
    }
    else
    {
        return false;
    }

    ZeroMemory(&mappedSubresource, sizeof(D3D11_MAPPED_SUBRESOURCE));
    hr = deviceContext->Map(m_PointLightBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSubresource);
    if (SUCCEEDED(hr))
    {
        memcpy(mappedSubresource.pData, m_PointLights, sizeof(PointLight) * m_RayTracingConstants.pointLightCount);
        deviceContext->Unmap(m_PointLightBuffer.Get(), 0);
    }
    else
    {
        return false;
    }

    ZeroMemory(&mappedSubresource, sizeof(D3D11_MAPPED_SUBRESOURCE));
    hr = deviceContext->Map(m_SamplesBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSubresource);
    if (SUCCEEDED(hr))
    {
        float* samples = reinterpret_cast<float*>(mappedSubresource.pData);
        for (int i = 0; i < kMaxSamplesCount; ++i)
            samples[i] = m_UniformRealDistribution(m_MersenneURBG);

        deviceContext->Unmap(m_SamplesBuffer.Get(), 0);
    }
    else
    {
        return false;
    }

    return true;
}

void Scene::DispatchRayTracing()
{
    ID3D11DeviceContext* deviceContext = GetDeviceContext();

    deviceContext->CSSetShader(m_RayTracingComputeShader.Get(), nullptr, 0);

    ID3D11UnorderedAccessView* rawFilmTextureUAV = m_FilmTextureUAV.Get();
    deviceContext->CSSetUnorderedAccessViews(0, 1, &rawFilmTextureUAV, nullptr);

    ID3D11ShaderResourceView* rawSRVs[] = { m_SpheresSRV.Get(), m_PointLightsSRV.Get(), m_RayTracingConstantsSRV.Get(), m_SamplesSRV.Get() };
    deviceContext->CSSetShaderResources(0, 4, rawSRVs);

    UINT threadGroupCountX = (UINT)ceil(m_RayTracingConstants.resolution.x / 32.0f);
    UINT threadGroupCountY = (UINT)ceil(m_RayTracingConstants.resolution.y / 32.0f);
    deviceContext->Dispatch(threadGroupCountX, threadGroupCountY, 1);

    rawFilmTextureUAV = nullptr;
    deviceContext->CSSetUnorderedAccessViews(0, 1, &rawFilmTextureUAV, nullptr);
}

void Scene::ClearFilmTexture()
{
    ID3D11DeviceContext* deviceContext = GetDeviceContext();
    const static float kClearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    deviceContext->ClearRenderTargetView(m_FilmTextureRenderTargetView.Get(), kClearColor);
}




