#ifndef _INTERSECTION_H_
#define _INTERSECTION_H_

struct Intersection
{
    float3  albedo;
    float3  specular;
    float3  emission;
    float   alpha;
    float3  position;
    float3  normal;
    float3  tangent;
    float   rayEpsilon;
    float   ior;
    bool    backface;
};

#endif