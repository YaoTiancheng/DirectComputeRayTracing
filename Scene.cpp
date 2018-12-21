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
    samplerDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    hr = device->CreateSamplerState(&samplerDesc, &m_CopySamplerState);
    if (FAILED(hr))
        return false;

    m_ResolutionWidth = resolutionWidth;
    m_ResolutionHeight = resolutionHeight;
    m_DefaultViewport = { 0.0f, 0.0f, (float)resolutionWidth, (float)resolutionHeight, 0.0f, 1.0f };

    return true;
}

void Scene::ResetScene()
{
    m_Spheres[0].center = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
    m_Spheres[0].radius = 0.5f;
    m_Spheres[0].albedo = XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f);
    m_RayTracingConstants.sphereCount = 1;
}

void Scene::AddOneSampleAndRender()
{
    AddOneSample();
    OnPostProcessing();
}

void Scene::AddOneSample()
{
    ID3D11DeviceContext* deviceContext = GetDeviceContext();

    deviceContext->CSSetShader(m_RayTracingComputeShader.Get(), nullptr, 0);

    ID3D11UnorderedAccessView* rawFilmTextureUAV = m_FilmTextureUAV.Get();
    deviceContext->CSSetUnorderedAccessViews(0, 1, &rawFilmTextureUAV, nullptr);

    ID3D11ShaderResourceView* rawSRVs[] = { m_SpheresSRV.Get(), m_RayTracingConstantsSRV.Get(), m_SamplesSRV.Get() };
    deviceContext->CSSetShaderResources(0, 3, rawSRVs);

    UINT threadGroupCountX = (UINT)ceil(m_ResolutionWidth / 16.0f);
    UINT threadGroupCountY = (UINT)ceil(m_ResolutionHeight / 16.0f);
    deviceContext->Dispatch(threadGroupCountX, threadGroupCountY, 1);

    rawFilmTextureUAV = nullptr;
    deviceContext->CSSetUnorderedAccessViews(0, 1, &rawFilmTextureUAV, nullptr);
}

void Scene::OnPostProcessing()
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
}


