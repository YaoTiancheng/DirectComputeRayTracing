#pragma once

#include "GPUResourceCreationFlag.h"

class GPUBuffer
{
public:
    static GPUBuffer*           Create( uint32_t byteWidth, uint32_t byteStride, uint32_t flags, const void* initialData = nullptr );

    ~GPUBuffer();

    ID3D11Buffer*               GetBuffer() const { return m_Buffer; }

    ID3D11ShaderResourceView*   GetSRV() const { return m_SRV; }

    ID3D11UnorderedAccessView*  GetUAV() const { return m_UAV; }

    void*                       Map();

    void                        Unmap();

private:
    GPUBuffer();

    ID3D11Buffer* m_Buffer;
    ID3D11ShaderResourceView*   m_SRV;
    ID3D11UnorderedAccessView * m_UAV;
};