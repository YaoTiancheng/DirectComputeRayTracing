
bool RaySphereIntersect(float4 origin
    , float4 direction
    , Sphere sphere
    , out float t)
{
    float radius2 = sphere.radius * sphere.radius;
    float t0, t1;
    float4 l = sphere.position - origin;
    float tca = dot(l, direction);
    float d2 = dot(l, l) - tca * tca;
    if (d2 > radius2)
        return false;
    float thc = sqrt(radius2 - d2);
    t0 = tca - thc;
    t1 = tca + thc;

    if (t0 < 0)
    {
        t0 = t1;
        if (t0 < 0)
            return false;
    }
    t = t0;
    return true;
}

bool RaySphereIntersect(float4 origin
    , float4 direction
    , Sphere sphere
    , out float t
    , out Intersection intersection)
{
    bool intersect = false;
    if (intersect = RaySphereIntersect(origin, direction, sphere, t))
    {
        intersection.position = origin + t * direction;
        intersection.normal = normalize(intersection.position - sphere.position);
        intersection.tangent = normalize(float4(cross(float3(0.0f, 1.0f, 0.0f), intersection.normal.xyz), 0.0f));
        if (isinf(intersection.tangent.x))
            intersection.tangent = float4(1.0f, 0.0f, 0.0f, 0.0f);
        intersection.albedo = sphere.albedo;
        intersection.specular = lerp(1.0f, sphere.albedo, sphere.metallic);
        intersection.emission = sphere.emission;
        intersection.alpha = 0.1f;
        intersection.rayEpsilon = 1e-3f * t;
        intersection.f0 = lerp(0.1f, 1.0f, sphere.metallic);
    }
    return intersect;
}
