#pragma once

#if defined( __cplusplus )

namespace GPU
{
    using uint = uint32_t;
    using float2 = DirectX::XMFLOAT2;
    using float3 = DirectX::XMFLOAT3;
    using float4 = DirectX::XMFLOAT4;
    using float4x3 = DirectX::XMFLOAT4X3;
}

#define GPU_STRUCTURE_NAMESPACE_BEGIN namespace GPU {
#define GPU_STRUCTURE_NAMESPACE_END }

#else 

#define GPU_STRUCTURE_NAMESPACE_BEGIN
#define GPU_STRUCTURE_NAMESPACE_END

#endif