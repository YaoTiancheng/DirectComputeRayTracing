#ifndef _LIGHT_H_
#define _LIGHT_H_

#include "CppTypes.h"

#define LIGHT_FLAGS_POINT_LIGHT 0x1

struct SLight
{
    float4x3    transform;
    float3      color;
    uint        flags;
};

#endif
