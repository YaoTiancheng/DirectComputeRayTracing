#pragma once

enum GPUResourceCreationFlags : uint32_t
{
      GPUResourceCreationFlags_None                 = 0x0
    , GPUResourceCreationFlags_IsConstantBuffer     = 0x1
    , GPUResourceCreationFlags_IsStructureBuffer    = 0x2
    , GPUResourceCreationFlags_IsVertexBuffer       = 0x4
    , GPUResourceCreationFlags_IsImmutable          = 0x8
    , GPUResourceCreationFlags_CPUWriteable         = 0x10
    , GPUResourceCreationFlags_HasUAV               = 0x20
    , GPUResourceCreationFlags_IsRenderTarget       = 0x40
    , GPUResourceCreationFlags_IndirectArgs         = 0x80
};