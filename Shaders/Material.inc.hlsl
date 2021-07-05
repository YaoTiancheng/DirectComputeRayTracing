#ifndef _MATERIAL_H_
#define _MATERIAL_H_

#include "CppTypes.h"

#define MATERIAL_FLAG_ALBEDO_TEXTURE    0x1
#define MATERIAL_FLAG_ROUGHNESS_TEXTURE 0x2
#define MATERIAL_FLAG_EMISSION_TEXTURE  0x4

struct Material
{
    float3 albedo;
    float3 emission;
    float  roughness;
    float  ior;
    float  transmission;
    float2 texTiling;
    uint   flags;
};

#endif
