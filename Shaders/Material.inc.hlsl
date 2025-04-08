#ifndef _MATERIAL_H_
#define _MATERIAL_H_

#include "CppTypes.h"

#define MATERIAL_FLAG_ALBEDO_TEXTURE    0x10
#define MATERIAL_FLAG_ROUGHNESS_TEXTURE 0x20
#define MATERIAL_FLAG_IS_TWOSIDED       0x40
#define MATERIAL_FLAG_MULTISCATTERING   0x80

#define MATERIAL_FLAG_TYPE_MASK         0x0000000F

#define MATERIAL_TYPE_DIFFUSE 0
#define MATERIAL_TYPE_PLASTIC 1
#define MATERIAL_TYPE_CONDUCTOR 2
#define MATERIAL_TYPE_DIELECTRIC 3

GPU_STRUCTURE_NAMESPACE_BEGIN

struct Material
{
    float3 albedo;
    int    albedoTextureIndex;
    float3 ior;
    float  roughness;
    float2 texTiling;
    uint   flags;
};

GPU_STRUCTURE_NAMESPACE_END

#endif
