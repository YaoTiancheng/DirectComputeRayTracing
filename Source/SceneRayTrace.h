#pragma once

struct SRayHit
{
    float m_T;
    float m_U;
    float m_V;
    uint32_t m_InstanceIndex;
    uint32_t m_MeshIndex;
    uint32_t m_TriangleIndex;
};

struct SRayTraversalCounters
{
    uint32_t m_TriangleTestsCount;
    uint32_t m_BoundingBoxTestsCount;
    uint32_t m_BLASEnteringsCount;
    uint32_t m_BLASLeafTestsCount;
};
