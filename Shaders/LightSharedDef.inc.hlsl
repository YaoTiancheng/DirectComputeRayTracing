#ifndef _LIGHT_SHARED_DEF_H_
#define _LIGHT_SHARED_DEF_H_

#include "CppTypes.h"

#define LIGHT_INDEX_INVALID -1

#define LIGHT_FLAGS_POINT_LIGHT 0x1
#define LIGHT_FLAGS_MESH_LIGHT 0x2
#define LIGHT_FLAGS_ENVIRONMENT_LIGHT 0x4

GPU_STRUCTURE_NAMESPACE_BEGIN

struct SLight
{
    float3 radiance;
    float3 position_or_triangleRange;
    uint flags;
};

#ifndef __cplusplus
float3 Light_GetPosition( SLight light )
{
    return light.position_or_triangleRange;
}

uint Light_GetTriangleOffset( SLight light )
{
    return asuint( light.position_or_triangleRange.x );
}

uint Light_GetTriangleCount( SLight light )
{
    return asuint( light.position_or_triangleRange.y );
}

uint Light_GetInstanceIndex( SLight light )
{
    return asuint( light.position_or_triangleRange.z );
}
#endif

GPU_STRUCTURE_NAMESPACE_END

#endif
