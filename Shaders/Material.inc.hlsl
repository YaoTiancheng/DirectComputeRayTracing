#ifndef _MATERIAL_H_
#define _MATERIAL_H_

#include "CppTypes.h"

struct Material
{
    float3 albedo;
    float3 emission;
    float  roughness;
    float  ior;
};

#endif
