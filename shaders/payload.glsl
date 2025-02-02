//struct PathTracingPayload {
//    vec3 hitPos, hitNormal;
//    vec3 baseColour, emittedLight, directLight;
//    float roughness, metallic;
//    float transmissionFactor, ior;
//    bool thin;
//    float attenuationDistance;
//    vec3 attenuationColour;
//};

struct RayPayload {
    uint seed;
    vec3 origin, direction;
    vec3 reflectivity, lightSample, emittedLight;
    bool scatter;
};

struct ShadowPayload {
    bool shadowRayMiss;
};

struct EmissivePayload {
    uint instanceGeometryIdx, instancePrimitiveIdx;
    bool instanceHit;
    vec3 emittedLight;
};