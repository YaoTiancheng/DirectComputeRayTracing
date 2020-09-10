#ifndef _BVHNODE_H_
#define _BVHNODE_H_

#include "CppTypes.h"

struct BVHNode
{
    float3 bboxMin;
    float3 bboxMax;
    uint   childOrPrimIndex;
    uint   misc;
};

#endif
