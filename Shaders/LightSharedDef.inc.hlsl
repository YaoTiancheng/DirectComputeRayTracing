#ifndef _LIGHT_SHARED_DEF_H_
#define _LIGHT_SHARED_DEF_H_

#include "CppTypes.h"

#define LIGHT_FLAGS_POINT_LIGHT 0x1
#define LIGHT_FLAGS_TRIANGLE_LIGHT 0x2

GPU_STRUCTURE_NAMESPACE_BEGIN

struct SLight
{
    float4x3 transform;
    float3 color;
    uint flags;
    uint primitiveId;
    float invSurfaceArea;
};

GPU_STRUCTURE_NAMESPACE_END

#endif
