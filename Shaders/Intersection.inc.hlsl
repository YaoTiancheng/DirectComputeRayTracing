#ifndef _INTERSECTION_H_
#define _INTERSECTION_H_

struct Intersection
{
    float3  albedo;
    float3  specular;
    float   alpha;
    float3  position;
    float3  normal;
    float3  tangent;
    float3  geometryNormal;
    float3  ior;
    bool    isMetal;
    float   transmission;
    bool    backface;
    bool    isTwoSided;
    uint    lightIndex;
    uint    triangleIndex;
};

#endif