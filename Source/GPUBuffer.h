#pragma once

#include "D3D12Resource.h"
#include "D3D12DescriptorHandle.h"

enum class EGPUBufferUsage
{
    Default = 0,
    Dynamic = 1,
    Staging = 2,
};

enum EGPUBufferBindFlag : uint32_t
{
    EGPUBufferBindFlag_ConstantBuffer = 0x1,
    EGPUBufferBindFlag_ShaderResource = 0x2,
    EGPUBufferBindFlag_UnorderedAccess = 0x4,
};

class GPUBuffer : public CD3D12Resource
{
public:
    static GPUBuffer* Create( uint32_t byteWidth, uint32_t byteStride, DXGI_FORMAT format, EGPUBufferUsage usage, uint32_t bindFlags, bool isStructured,
        const void* initialData = nullptr, D3D12_RESOURCE_STATES resourceStates = D3D12_RESOURCE_STATE_COMMON );

    static GPUBuffer* Create( uint32_t byteWidth, uint32_t byteStride, DXGI_FORMAT format, EGPUBufferUsage usage, uint32_t bindFlags,
        const void* initialData = nullptr, D3D12_RESOURCE_STATES resourceStates = D3D12_RESOURCE_STATE_COMMON );

    static GPUBuffer* CreateStructured( uint32_t byteWidth, uint32_t byteStride, EGPUBufferUsage usage, uint32_t bindFlags,
        const void* initialData = nullptr, D3D12_RESOURCE_STATES resourceStates = D3D12_RESOURCE_STATE_COMMON );

    virtual ~GPUBuffer();

    ID3D12Resource* GetBuffer() const { return m_Buffer.Get(); }

    D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const { return m_Buffer->GetGPUVirtualAddress(); }

    const SD3D12DescriptorHandle& GetSRV() const { return m_SRV; }

    const SD3D12DescriptorHandle& GetUAV() const { return m_UAV; }

    const SD3D12DescriptorHandle& GetCBV() const { return m_CBV; }

    SD3D12DescriptorHandle GetSRV( DXGI_FORMAT format, uint32_t byteStride, uint32_t elementOffset, uint32_t numElement );

    void* Map();

    void Unmap();

    struct SUploadContext
    {
        uint8_t* Map();

        void Unmap();

        void Upload();

        bool IsValid() const { return m_SrcBuffer && m_DestBuffer; }

        ID3D12Resource* m_SrcBuffer = nullptr;
        ID3D12Resource* m_DestBuffer = nullptr;
        uint64_t m_SrcOffset = 0;
        uint64_t m_ByteWidth = 0;
        CD3D12ComPtr<ID3D12Resource> m_CommittedBuffer;
    };

    bool AllocateUploadContext( SUploadContext* context ) const;

private:
    ComPtr<ID3D12Resource> m_Buffer;
    SD3D12DescriptorHandle m_SRV;
    SD3D12DescriptorHandle m_UAV;
    SD3D12DescriptorHandle m_CBV;

    struct SSRVDesc
    {
        uint32_t m_ElementOffset;
        uint32_t m_NumElement;

        bool operator==( const SSRVDesc& other ) const
        {
            return m_ElementOffset == other.m_ElementOffset 
                && m_NumElement == m_NumElement;
        }
    };
    struct SRVDescHash
    {
        std::size_t operator()( const SSRVDesc& desc ) const noexcept
        {
            return std::hash<uint32_t>()( desc.m_ElementOffset ) ^ std::hash<uint32_t>()( desc.m_NumElement );
        }
    };
    std::unordered_map<SSRVDesc, SD3D12DescriptorHandle, SRVDescHash> m_SRVs;
};