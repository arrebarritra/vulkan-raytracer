struct PathTracingPayload {
    vec3 hitPos, hitNormal;
    vec3 baseColour, emittedLight, directLight;
    float roughness, metallic;
    float transmissionFactor, ior;
    bool thin;
    float attenuationDistance;
    vec3 attenuationColour;
};

struct ShadowPayload {
    bool shadowRayMiss;
};

struct EmissivePayload {
    uint instanceIdx;
    bool instanceHit;
    vec3 emittedLight;
};