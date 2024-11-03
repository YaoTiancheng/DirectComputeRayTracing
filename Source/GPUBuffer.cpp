#include "stdafx.h"
#include "GPUBuffer.h"
#include "D3D12Adapter.h"
#include "D3D12DescriptorPoolHeap.h"
#include "D3D12MemoryArena.h"

const CD3D12DescritorHandle& GPUBuffer::GetSRV( DXGI_FORMAT format, uint32_t byteStride, uint32_t elementOffset, uint32_t numElement )
{
    CD3D12DescritorHandle SRV;
    auto it = m_SRVs.find( { elementOffset, numElement } );
    if ( it == m_SRVs.end() )
    {
        CD3D12DescriptorPoolHeap* descriptorHeap = D3D12Adapter::GetDescriptorPoolHeap( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

        D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
        desc.Format = format;
        desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.Buffer.FirstElement = elementOffset;
        desc.Buffer.NumElements = numElement;
        desc.Buffer.StructureByteStride = byteStride;
        desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        SRV = descriptorHeap->Allocate( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
        if ( SRV.IsValid() )
        {
            D3D12Adapter::GetDevice()->CreateShaderResourceView( m_Buffer.Get(), &desc, SRV.CPU );
            m_SRVs.insert( { { elementOffset, numElement }, SRV } );
        }
    }
    else 
    {
        SRV = it->second;
    }
    assert( SRV.IsValid() );
    return SRV;
}

void* GPUBuffer::Map()
{
    void* mappedData = nullptr;
    if ( FAILED( m_Buffer->Map( 0, nullptr, &mappedData ) ) )
    {
        return nullptr;
    }
    return mappedData;
}

void GPUBuffer::Unmap()
{
    m_Buffer->Unmap( 0, nullptr );
}

uint8_t* GPUBuffer::SUploadContext::Map()
{
    void* mappedData = nullptr;
    if ( FAILED( m_SrcBuffer->Map( 0, nullptr, &mappedData ) ) )
    {
        return nullptr;
    }
    return (uint8_t*)mappedData + m_SrcOffset;
}

void GPUBuffer::SUploadContext::Unmap()
{
    D3D12_RANGE range;
    range.Begin = m_SrcOffset;
    range.End = m_SrcOffset + m_ByteWidth;
    m_SrcBuffer->Unmap( 0, &range );
}

void GPUBuffer::SUploadContext::Upload()
{
    ID3D12GraphicsCommandList* commandList = D3D12Adapter::GetCommandList();
    commandList->CopyBufferRegion( m_DestBuffer, 0, m_SrcBuffer, m_SrcOffset, m_ByteWidth );
}

namespace
{
    bool InternalAllocateUploadContext( ID3D12Resource* destBuffer, GPUBuffer::SUploadContext* context )
    {
        *context = {};

        const D3D12_RESOURCE_DESC desc = destBuffer->GetDesc();

        CD3D12BufferedGrowingBufferArena* uploadBufferArena = D3D12Adapter::GetUploadBufferArena();
        SD3D12ArenaBufferLocation location = uploadBufferArena->Allocate( desc.Width, 1 );
        if ( !location.IsValid() )
        {
            return false;
        }

        context->m_SrcBuffer = location.m_Memory;
        context->m_DestBuffer = destBuffer;
        context->m_SrcOffset = location.m_Offset;
        context->m_ByteWidth = desc.Width;

        return true;
    }
}

bool GPUBuffer::AllocateUploadContext( GPUBuffer::SUploadContext* context ) const
{
    return InternalAllocateUploadContext( m_Buffer.Get(), context );
}

GPUBuffer::~GPUBuffer()
{
    CD3D12DescriptorPoolHeap* descriptorHeap = D3D12Adapter::GetDescriptorPoolHeap( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
    for ( auto& it : m_SRVs )
    {
        descriptorHeap->Free( it.second, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
    }
    if ( m_UAV.IsValid() )
    {
        descriptorHeap->Free( m_UAV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
    }
    if ( m_CBV.IsValid() )
    {
        descriptorHeap->Free( m_CBV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
    }
}

GPUBuffer* GPUBuffer::Create( uint32_t byteWidth, uint32_t byteStride, DXGI_FORMAT format, EGPUBufferUsage usage, uint32_t bindFlags, bool isStructured, const void* initialData, D3D12_RESOURCE_STATES resourceStates )
{
    const bool hasUAV = ( bindFlags & EGPUBufferBindFlag_UnorderedAccess ) != 0;
    const bool hasSRV = ( bindFlags & EGPUBufferBindFlag_ShaderResource ) != 0;
    const bool hasCBV = ( bindFlags & EGPUBufferBindFlag_ConstantBuffer ) != 0;

    ComPtr<ID3D12Resource> buffer;
    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = byteWidth;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = format;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags = hasUAV ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
    if ( usage != EGPUBufferUsage::Dynamic )
    {
        const D3D12_HEAP_TYPE heapType = usage == EGPUBufferUsage::Default ? D3D12_HEAP_TYPE_DEFAULT : D3D12_HEAP_TYPE_READBACK;
        const D3D12_RESOURCE_STATES resourceStates = usage == EGPUBufferUsage::Default && !initialData ? resourceStates : D3D12_RESOURCE_STATE_COPY_DEST;
        if ( FAILED( D3D12Adapter::GetDevice()->CreateCommittedResource( &CD3DX12_HEAP_PROPERTIES( heapType ), D3D12_HEAP_FLAG_NONE, &bufferDesc,
            resourceStates, nullptr, IID_PPV_ARGS( buffer.GetAddressOf() ) ) ) )
        {
            return nullptr;
        }
    }
    else
    {
        // todo: Placed resource for constant buffers seems to be an overkill. Use buffer sub-allocation instead.

        CD3D12BufferedGrowingHeapArena* uploadHeapArena = D3D12Adapter::GetUploadHeapArena();
        SD3D12ArenaHeapLocation location = uploadHeapArena->Allocate( byteWidth, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT );
        if ( !location.IsValid() )
        {
            return nullptr;
        }

        if ( FAILED( D3D12Adapter::GetDevice()->CreatePlacedResource( location.m_Memory, location.m_Offset, &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS( buffer.GetAddressOf() ) ) ) )
        {
            return nullptr;
        }
    }

    if ( initialData )
    {
        assert( usage != EGPUBufferUsage::Staging );
        if ( usage == EGPUBufferUsage::Dynamic )
        {
            void* mappedData = nullptr;
            if ( FAILED( buffer->Map( 0, nullptr, &mappedData ) ) )
            {
                return nullptr;
            }
            memcpy_s( mappedData, byteWidth, initialData, byteWidth );
            buffer->Unmap( 0, nullptr );
        }
        else if ( usage == EGPUBufferUsage::Default )
        {
            SUploadContext uploadContext;
            if ( !InternalAllocateUploadContext( buffer.Get(), &uploadContext ) )
            {
                return nullptr;
            }

            uint8_t* mappedData = uploadContext.Map();
            if ( mappedData )
            { 
                memcpy_s( mappedData, byteWidth, initialData, byteWidth );
                uploadContext.Unmap();
                uploadContext.Upload();
            }

            if ( resourceStates != D3D12_RESOURCE_STATE_COPY_DEST )
            {
                ID3D12GraphicsCommandList* commandList = D3D12Adapter::GetCommandList();
                D3D12_RESOURCE_BARRIER barrier = {};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                barrier.Transition.pResource = buffer.Get();
                barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
                barrier.Transition.StateAfter = resourceStates;
                commandList->ResourceBarrier( 1, &barrier );
            }
        }
    }

    CD3D12DescriptorPoolHeap* descriptorHeap = D3D12Adapter::GetDescriptorPoolHeap( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

    CD3D12DescritorHandle SRV;
    if ( hasSRV )
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
        desc.Format = isStructured ? DXGI_FORMAT_UNKNOWN : format;
        desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.Buffer.FirstElement = 0;
        desc.Buffer.NumElements = byteWidth / byteStride;
        desc.Buffer.StructureByteStride = isStructured ? byteStride : 0;
        desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        SRV = descriptorHeap->Allocate( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
        if ( !SRV.IsValid() )
        {
            return nullptr;
        }
        D3D12Adapter::GetDevice()->CreateShaderResourceView( buffer.Get(), &desc, SRV.CPU );
    }

    CD3D12DescritorHandle UAV;
    if ( hasUAV )
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC desc;
        desc.Format = isStructured ? DXGI_FORMAT_UNKNOWN : format;
        desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        desc.Buffer.FirstElement = 0;
        desc.Buffer.NumElements = byteWidth / byteStride;
        desc.Buffer.StructureByteStride = isStructured ? byteStride : 0;
        desc.Buffer.CounterOffsetInBytes = 0;
        desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
        UAV = descriptorHeap->Allocate( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
        if ( !UAV.IsValid() )
        {
            descriptorHeap->Free( SRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
            return nullptr;
        }
        D3D12Adapter::GetDevice()->CreateUnorderedAccessView( buffer.Get(), nullptr, &desc, UAV.CPU );
    }

    CD3D12DescritorHandle CBV;
    if ( hasCBV )
    {
        D3D12_CONSTANT_BUFFER_VIEW_DESC desc;
        desc.BufferLocation = buffer->GetGPUVirtualAddress();
        desc.SizeInBytes = byteWidth;
        CBV = descriptorHeap->Allocate( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
        if ( !CBV.IsValid() )
        {
            descriptorHeap->Free( SRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
            descriptorHeap->Free( UAV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
            return nullptr;
        }
        D3D12Adapter::GetDevice()->CreateConstantBufferView( &desc, CBV.CPU );
    }

    GPUBuffer* gpuBuffer = new GPUBuffer();
    gpuBuffer->m_Buffer = buffer;
    gpuBuffer->m_SRV = SRV;
    gpuBuffer->m_UAV = UAV;
    gpuBuffer->m_CBV = CBV;

    if ( SRV.IsValid() )
    {
        gpuBuffer->m_SRVs.insert( { { 0, byteWidth / byteStride }, SRV } );
    }

    return gpuBuffer;
}

GPUBuffer* GPUBuffer::CreateStructured( uint32_t byteWidth, uint32_t byteStride, EGPUBufferUsage usage, uint32_t bindFlags, const void* initialData, D3D12_RESOURCE_STATES resourceStates )
{
    return Create( byteWidth, byteStride, DXGI_FORMAT_UNKNOWN, usage, bindFlags, true, initialData, resourceStates );
}

GPUBuffer* GPUBuffer::Create( uint32_t byteWidth, uint32_t byteStride, DXGI_FORMAT format, EGPUBufferUsage usage, uint32_t bindFlags, const void* initialData, D3D12_RESOURCE_STATES resourceStates )
{
    return Create( byteWidth, byteStride, format, usage, bindFlags, false, initialData, resourceStates );
}