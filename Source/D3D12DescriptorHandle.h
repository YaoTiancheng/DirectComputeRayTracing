#pragma once

struct CD3D12DescritorHandle
{
    CD3D12DescritorHandle()
        : CPU( CD3DX12_DEFAULT() )
        , GPU( CD3DX12_DEFAULT() )
    {
    }

    bool IsValid() const { return CPU.ptr != 0 && GPU.ptr != 0; }

    operator bool() const { return IsValid(); }

    void InitOffseted( ID3D12DescriptorHeap* heap, uint32_t offsetInDescriptors, uint32_t descriptorIncrementSize )
    {
        CPU.InitOffsetted( heap->GetCPUDescriptorHandleForHeapStart(), offsetInDescriptors, descriptorIncrementSize );
        GPU.InitOffsetted( heap->GetGPUDescriptorHandleForHeapStart(), offsetInDescriptors, descriptorIncrementSize );
    }

    void Offset( uint32_t offsetInDescriptors, uint32_t descriptorIncrementSize )
    {
        CPU.Offset( offsetInDescriptors, descriptorIncrementSize );
        GPU.Offset( offsetInDescriptors, descriptorIncrementSize );
    }

    void Offsetted( uint32_t offsetInDescriptors, uint32_t descriptorIncrementSize ) const
    {
        CD3D12DescritorHandle result = *this;
        result.Offset( offsetInDescriptors, descriptorIncrementSize );
        return result;
    }

    CD3DX12_CPU_DESCRIPTOR_HANDLE CPU;
    CD3DX12_GPU_DESCRIPTOR_HANDLE GPU;
};