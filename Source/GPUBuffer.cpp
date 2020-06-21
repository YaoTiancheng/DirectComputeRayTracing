#include "stdafx.h"
#include "GPUBuffer.h"
#include "D3D11RenderSystem.h"

void* GPUBuffer::Map()
{
    D3D11_MAPPED_SUBRESOURCE mappedSubresource;
    ZeroMemory( &mappedSubresource, sizeof( D3D11_MAPPED_SUBRESOURCE ) );
    HRESULT hr = GetDeviceContext()->Map( m_Buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSubresource );
    if ( FAILED( hr ) )
        return nullptr;
    return mappedSubresource.pData;
}

void GPUBuffer::Unmap()
{
    GetDeviceContext()->Unmap( m_Buffer, 0 );
}

GPUBuffer::GPUBuffer()
    : m_Buffer( nullptr )
    , m_SRV( nullptr )
{
}

GPUBuffer::~GPUBuffer()
{
    if ( m_SRV )
    {
        m_SRV->Release();
        m_SRV = nullptr;
    }
    if ( m_Buffer )
    {
        m_Buffer->Release();
        m_Buffer = nullptr;
    }
}

GPUBuffer* GPUBuffer::Create( uint32_t byteWidth, uint32_t byteStride, uint32_t flags, const void* initialData )
{
    bool isCBuffer          = ( flags & GPUBufferCreationFlags::GPUBufferCreationFlags_IsConstantBuffer ) != 0;
    bool isImmutable        = ( flags & GPUBufferCreationFlags::GPUBufferCreationFlags_IsImmutable ) != 0;
    bool CPUWriteable       = ( flags & GPUBufferCreationFlags::GPUBufferCreationFlags_CPUWriteable ) != 0;
    bool isStructureBuffer  = ( flags & GPUBufferCreationFlags::GPUBufferCreationFlags_IsStructureBuffer ) != 0;
    bool isVertexBuffer     = ( flags & GPUBufferCreationFlags::GPUBufferCreationFlags_IsVertexBuffer ) != 0;

    D3D11_BUFFER_DESC bufferDesc;
    ZeroMemory( &bufferDesc, sizeof( bufferDesc ) );
    bufferDesc.ByteWidth            = byteWidth;
    bufferDesc.Usage                = isImmutable ? D3D11_USAGE_IMMUTABLE : D3D11_USAGE_DYNAMIC;
    uint32_t bindFlags              = 0;
    if ( isCBuffer )
        bindFlags = D3D11_BIND_CONSTANT_BUFFER;
    else if ( isVertexBuffer )
        bindFlags = D3D11_BIND_VERTEX_BUFFER;
    else 
        bindFlags = D3D11_BIND_SHADER_RESOURCE;
    bufferDesc.BindFlags            = bindFlags;
    bufferDesc.CPUAccessFlags       = CPUWriteable ? D3D11_CPU_ACCESS_WRITE : 0;
    bufferDesc.StructureByteStride  = byteStride;
    bufferDesc.MiscFlags            = isStructureBuffer ? D3D11_RESOURCE_MISC_BUFFER_STRUCTURED : 0;

    D3D11_SUBRESOURCE_DATA subresourceData;
    subresourceData.pSysMem = initialData;
    subresourceData.SysMemPitch = 0;
    subresourceData.SysMemSlicePitch = 0;

    ID3D11Buffer* buffer = nullptr;
    D3D11_SUBRESOURCE_DATA* initialDataPtr = initialData != nullptr ? &subresourceData : nullptr;
    HRESULT hr = GetDevice()->CreateBuffer( &bufferDesc, initialDataPtr, &buffer );
    if ( FAILED( hr ) )
    {
        return nullptr;
    }

    ID3D11ShaderResourceView* SRV = nullptr;
    if ( !isCBuffer && !isVertexBuffer )
    {
        D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc;
        SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
        SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        SRVDesc.Buffer.ElementOffset = 0;
        SRVDesc.Buffer.NumElements = byteWidth / byteStride;
        hr = GetDevice()->CreateShaderResourceView( buffer, &SRVDesc, &SRV );
        if (FAILED( hr ))
        {
            buffer->Release();
            return nullptr;
        }
    }

    GPUBuffer* gpuBuffer = new GPUBuffer();
    gpuBuffer->m_Buffer = buffer;
    gpuBuffer->m_SRV = SRV;

    return gpuBuffer;
}
