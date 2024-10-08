#pragma once

struct CD3D12DescritorHandle
{
    CD3D12DescritorHandle()
        : CPU( CD3DX12_DEFAULT() )
        , GPU( CD3DX12_DEFAULT() )
    {
    }

    bool IsValid() const { return CPU.ptr != 0 && GPU.ptr != 0; }

    void InitOffseted( ID3D12DescriptorHeap* heap, uint32_t offsetInDescriptors, uint32_t descriptorIncrementSize )
    {
        CPU.InitOffsetted( heap->GetCPUDescriptorHandleForHeapStart(), offsetInDescriptors, descriptorIncrementSize );
        GPU.InitOffsetted( heap->GetGPUDescriptorHandleForHeapStart(), offsetInDescriptors, descriptorIncrementSize );
    }

    CD3DX12_CPU_DESCRIPTOR_HANDLE CPU;
    CD3DX12_GPU_DESCRIPTOR_HANDLE GPU;
};