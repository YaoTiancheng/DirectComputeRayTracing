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
    , m_UAV( nullptr )
{
}

GPUBuffer::~GPUBuffer()
{
    if ( m_SRV )
    {
        m_SRV->Release();
        m_SRV = nullptr;
    }
    if ( m_UAV )
    {
        m_UAV->Release();
        m_UAV = nullptr;
    }
    if ( m_Buffer )
    {
        m_Buffer->Release();
        m_Buffer = nullptr;
    }
}

GPUBuffer* GPUBuffer::Create( uint32_t byteWidth, uint32_t byteStride, DXGI_FORMAT format, D3D11_USAGE usage, uint32_t bindFlags, bool isStructured, uint32_t flags, const void* initialData )
{
    bool CPUWriteable = ( flags & GPUResourceCreationFlags::GPUResourceCreationFlags_CPUWriteable ) != 0;
    bool hasUAV = ( bindFlags & D3D11_BIND_UNORDERED_ACCESS ) != 0;
    bool hasSRV = ( bindFlags & D3D11_BIND_SHADER_RESOURCE ) != 0;

    D3D11_BUFFER_DESC bufferDesc;
    ZeroMemory( &bufferDesc, sizeof( bufferDesc ) );
    bufferDesc.ByteWidth = byteWidth;
    bufferDesc.Usage = usage;
    bufferDesc.BindFlags = bindFlags;
    bufferDesc.CPUAccessFlags = CPUWriteable ? D3D11_CPU_ACCESS_WRITE : 0;
    bufferDesc.StructureByteStride = byteStride;
    bufferDesc.MiscFlags = isStructured ? D3D11_RESOURCE_MISC_BUFFER_STRUCTURED : 0;

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
    if ( hasSRV )
    {
        D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc;
        SRVDesc.Format = isStructured ? DXGI_FORMAT_UNKNOWN : format;
        SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        SRVDesc.Buffer.ElementOffset = 0;
        SRVDesc.Buffer.NumElements = byteWidth / byteStride;
        hr = GetDevice()->CreateShaderResourceView( buffer, &SRVDesc, &SRV );
        if ( FAILED( hr ) )
        {
            buffer->Release();
            return nullptr;
        }
    }

    ID3D11UnorderedAccessView* UAV = nullptr;
    if ( hasUAV )
    {
        D3D11_UNORDERED_ACCESS_VIEW_DESC UAVDesc;
        UAVDesc.Format = isStructured ? DXGI_FORMAT_UNKNOWN : format;
        UAVDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        UAVDesc.Buffer.FirstElement = 0;
        UAVDesc.Buffer.NumElements = byteWidth / byteStride;
        UAVDesc.Buffer.Flags = 0;
        hr = GetDevice()->CreateUnorderedAccessView( buffer, &UAVDesc, &UAV );
        if ( FAILED( hr ) )
        {
            UAV->Release();
            buffer->Release();
            return nullptr;
        }
    }

    GPUBuffer* gpuBuffer = new GPUBuffer();
    gpuBuffer->m_Buffer = buffer;
    gpuBuffer->m_SRV = SRV;
    gpuBuffer->m_UAV = UAV;

    return gpuBuffer;
}

GPUBuffer* GPUBuffer::CreateStructured( uint32_t byteWidth, uint32_t byteStride, D3D11_USAGE usage, uint32_t bindFlags, uint32_t flags, const void* initialData )
{
    return Create( byteWidth, byteStride, DXGI_FORMAT_UNKNOWN, usage, bindFlags, true, flags, initialData );
}

GPUBuffer* GPUBuffer::Create( uint32_t byteWidth, uint32_t byteStride, DXGI_FORMAT format, D3D11_USAGE usage, uint32_t bindFlags, uint32_t flags, const void* initialData )
{
    return Create( byteWidth, byteStride, format, usage, bindFlags, false, flags, initialData );
}
