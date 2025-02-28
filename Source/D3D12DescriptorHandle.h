#pragma once

struct SD3D12DescriptorHandle
{
    SD3D12DescriptorHandle()
        : CPU( CD3DX12_DEFAULT() )
    {
    }

    SD3D12DescriptorHandle( D3D12_CPU_DESCRIPTOR_HANDLE handle )
        : CPU( handle )
    {
    }

    bool IsValid() const { return CPU.ptr != 0; }

    operator bool() const { return IsValid(); }

    operator D3D12_CPU_DESCRIPTOR_HANDLE() { return CPU; }

    void InitOffseted( ID3D12DescriptorHeap* heap, uint32_t offsetInDescriptors, uint32_t descriptorIncrementSize )
    {
        CPU.InitOffsetted( heap->GetCPUDescriptorHandleForHeapStart(), offsetInDescriptors, descriptorIncrementSize );
    }

    void Offset( uint32_t offsetInDescriptors, uint32_t descriptorIncrementSize )
    {
        CPU.Offset( offsetInDescriptors, descriptorIncrementSize );
    }

    void Offsetted( uint32_t offsetInDescriptors, uint32_t descriptorIncrementSize ) const
    {
        SD3D12DescriptorHandle result = *this;
        result.Offset( offsetInDescriptors, descriptorIncrementSize );
        return result;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE CPU;
};