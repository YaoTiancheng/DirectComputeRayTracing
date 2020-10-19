#pragma once

class ComputeShader;
class GfxShader;
class GPUBuffer;

namespace RenderJobHelper
{
    template < typename T >
    struct ResourceList
    {
        ResourceList( const T* address, uint32_t count )
            : m_Address( address )
            , m_Count( count )
        {
        }

        const T* m_Address;
        uint32_t m_Count;
    };

    using ResourceViewList          = ResourceList<ID3D11ShaderResourceView*>;
    using UnorderedAccessViewList   = ResourceList<ID3D11UnorderedAccessView*>;
    using SamplerStateList          = ResourceList<ID3D11SamplerState*>;
    using BufferList                = ResourceList<ID3D11Buffer*>;

    void DispatchCompute( uint32_t dispatchSizeX, uint32_t dispatchSizeY, uint32_t dispatchSizeZ, ComputeShader* shader, const ResourceViewList& SRVs, const UnorderedAccessViewList& UAVs, const SamplerStateList& samplers, const BufferList& constantBuffers );

    void DispatchDraw( GPUBuffer* vertexBuffer, GfxShader* shader, ID3D11InputLayout* inputLayout, const ResourceViewList& SRVs, const SamplerStateList& samplers, const BufferList& constantBuffers, uint32_t vertexCount, uint32_t vertexStride );
}