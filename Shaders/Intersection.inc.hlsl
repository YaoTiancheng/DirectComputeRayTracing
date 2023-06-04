#ifndef _INTERSECTION_H_
#define _INTERSECTION_H_

struct Intersection
{
    float3  albedo;
    float   alpha;
    float3  position;
    float3  normal;
    float3  tangent;
    float3  geometryNormal;
    float3  ior;
    bool    isTwoSided;
    bool    backface;
    bool    multiscattering;
    uint    materialType;
    uint    lightIndex;
    uint    triangleIndex;
};

#endif