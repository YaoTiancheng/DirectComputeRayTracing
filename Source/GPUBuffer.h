#pragma once

#include "GPUResourceCreationFlag.h"

class GPUBuffer
{
public:
    static GPUBuffer*           Create( uint32_t byteWidth, uint32_t byteStride, DXGI_FORMAT format, D3D11_USAGE usage, uint32_t bindFlags, bool isStructured, uint32_t flags = (uint32_t)GPUResourceCreationFlags::GPUResourceCreationFlags_None, const void* initialData = nullptr );

    static GPUBuffer*           Create( uint32_t byteWidth, uint32_t byteStride, DXGI_FORMAT format, D3D11_USAGE usage, uint32_t bindFlags, uint32_t flags = (uint32_t)GPUResourceCreationFlags::GPUResourceCreationFlags_None, const void* initialData = nullptr );

    static GPUBuffer*           CreateStructured( uint32_t byteWidth, uint32_t byteStride, D3D11_USAGE usage, uint32_t bindFlags, uint32_t flags = (uint32_t)GPUResourceCreationFlags::GPUResourceCreationFlags_None, const void* initialData = nullptr );

    ~GPUBuffer();

    ID3D11Buffer*               GetBuffer() const { return m_Buffer; }

    ID3D11ShaderResourceView*   GetSRV() const { assert( m_SRV != nullptr ); return m_SRV; }

    ID3D11UnorderedAccessView*  GetUAV() const { assert( m_UAV != nullptr ); return m_UAV; }

    ID3D11ShaderResourceView*   GetSRV( uint32_t elementOffset, uint32_t numElement );

    void*                       Map();

    void*                       Map( D3D11_MAP mapType, uint32_t flags );

    void                        Unmap();

private:
    GPUBuffer();

    ID3D11Buffer* m_Buffer;
    ID3D11ShaderResourceView*   m_SRV;
    ID3D11UnorderedAccessView * m_UAV;

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
    std::unordered_map<SSRVDesc, ID3D11ShaderResourceView*, SRVDescHash> m_SRVs;
};