#include "stdafx.h"
#include "Scene.h"
#include "D3D11RenderSystem.h"


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
    textureDesc.BindFlags           = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    HRESULT hr = device->CreateTexture2D(&textureDesc, nullptr, &m_FilmTexture);
    if (FAILED(hr))
        return false;

    hr = device->CreateUnorderedAccessView(m_FilmTexture.Get(), nullptr, &m_FilmTextureUAV);
    if (FAILED(hr))
        return false;

    hr = device->CreateShaderResourceView(m_FilmTexture.Get(), nullptr, &m_FilmTextureSRV);
    if (FAILED(hr))
        return false;

    ID3DBlob* csBlob = CompileFromFile(L"RayTracing.hlsl", "main", "cs_5_0");
    if (!csBlob)
        return false;

    m_RayTracingComputeShader.Attach(CreateComputeShader(csBlob));
    csBlob->Release();
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

    for (auto& sample : m_Samples)
        sample = (float)rand() / RAND_MAX;

    bufferDesc.ByteWidth = sizeof(m_Samples);
    bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
    bufferDesc.CPUAccessFlags = 0;
    bufferDesc.StructureByteStride = sizeof(float);
    D3D11_SUBRESOURCE_DATA subresourceData;
    subresourceData.pSysMem = m_Samples;
    subresourceData.SysMemPitch = 0;
    subresourceData.SysMemSlicePitch = 0;
    hr = device->CreateBuffer(&bufferDesc, &subresourceData, &m_SamplesBuffer);
    if (FAILED(hr))
        return false;

    bufferDesc.ByteWidth = sizeof(m_Spheres);
    bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bufferDesc.StructureByteStride = sizeof(Sphere);
    hr = device->CreateBuffer(&bufferDesc, nullptr, &m_SpheresBuffer);
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

    return true;
}
