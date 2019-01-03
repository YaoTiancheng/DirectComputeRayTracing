
bool RaySphereIntersect(float4 origin
    , float4 direction
    , Sphere sphere
    , out float t)
{
    float radius2 = sphere.radius * sphere.radius;
    float t0, t1;
    float4 l = sphere.position - origin;
    float tca = dot(l, direction);
    if (tca < 0.0f)
        return false;
    float d2 = dot(l, l) - tca * tca;
    if (d2 > radius2)
        return false;
    float thc = sqrt(radius2 - d2);
    t0 = tca - thc;
    t1 = tca + thc;

    if (t0 > t1)
    {
        float temp = t0;
        t0 = t1;
        t1 = temp;
    }
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
    , out float4 position
    , out float4 normal
    , out float4 tangent
    , out float4 albedo
    , out float4 emission)
{
    bool intersect = false;
    if (intersect = RaySphereIntersect(origin, direction, sphere, t))
    {
        position = origin + t * direction;
        normal = normalize(position - sphere.position);
        tangent = normalize(float4(cross(float3(0.0f, 1.0f, 0.0f), normal.xyz), 0.0f));
        if (isinf(tangent.x))
            tangent = float4(1.0f, 0.0f, 0.0f, 0.0f);
        albedo = sphere.albedo;
        emission = sphere.emission;
    }
    return intersect;
}
