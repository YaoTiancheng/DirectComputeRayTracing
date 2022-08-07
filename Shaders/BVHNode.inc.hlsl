#ifndef _BVHNODE_H_
#define _BVHNODE_H_

#include "CppTypes.h"

GPU_STRUCTURE_NAMESPACE_BEGIN

struct BVHNode
{
    float3 bboxMin;
    float3 bboxMax;
    uint   childOrPrimIndex;
    uint   misc;
};

GPU_STRUCTURE_NAMESPACE_END

#endif
