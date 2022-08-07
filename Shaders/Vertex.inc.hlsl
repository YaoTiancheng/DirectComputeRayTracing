#ifndef _VERTEX_H_
#define _VERTEX_H_

#include "CppTypes.h"

GPU_STRUCTURE_NAMESPACE_BEGIN

struct Vertex
{
    float3  position;
    float3  normal;
    float3  tangent;
    float2  texcoord;
};

GPU_STRUCTURE_NAMESPACE_END

#endif