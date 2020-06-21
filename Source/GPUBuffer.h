#pragma once

enum GPUBufferCreationFlags : uint32_t
{
      GPUBufferCreationFlags_None              = 0x0
    , GPUBufferCreationFlags_IsConstantBuffer  = 0x1
    , GPUBufferCreationFlags_IsStructureBuffer = 0x2
    , GPUBufferCreationFlags_IsVertexBuffer    = 0x4
    , GPUBufferCreationFlags_IsImmutable       = 0x8
    , GPUBufferCreationFlags_CPUWriteable      = 0x10
};

class GPUBuffer
{
public:
    static GPUBuffer*           Create( uint32_t byteWidth, uint32_t byteStride, uint32_t flags, const void* initialData = nullptr );

    ~GPUBuffer();

    ID3D11Buffer*               GetBuffer() const { return m_Buffer; }

    ID3D11ShaderResourceView*   GetSRV() const { return m_SRV; }

    void*                       Map();

    void                        Unmap();

private:
    GPUBuffer();

    ID3D11Buffer* m_Buffer;
    ID3D11ShaderResourceView* m_SRV;
};