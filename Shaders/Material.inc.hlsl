#ifndef _MATERIAL_H_
#define _MATERIAL_H_

#include "CppTypes.h"

#define MATERIAL_FLAG_ALBEDO_TEXTURE    0x1
#define MATERIAL_FLAG_ROUGHNESS_TEXTURE 0x2
#define MATERIAL_FLAG_IS_METAL          0x4
#define MATERIAL_FLAG_IS_TWOSIDED       0x8

GPU_STRUCTURE_NAMESPACE_BEGIN

struct Material
{
    float3 albedo;
    float  roughness;
    float3 ior;
    float  transmission;
    float2 texTiling;
    uint   flags;
};

GPU_STRUCTURE_NAMESPACE_END

#endif
