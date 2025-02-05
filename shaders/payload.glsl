struct RayPayload {
    uint seed, bounce;
    vec3 origin, direction;
    vec3 reflectivity, lightSample, emittedLight;
    float materialSamplePDF;
    bool scatter;
};

struct ShadowPayload {
    uint seed;
    bool shadowRayMiss;
};

struct EmissivePayload {
    uint seed;
    uint instanceGeometryIdx, instancePrimitiveIdx;
    bool instanceHit;
    vec3 normal, emittedLight;
};

struct EmissivePDFPayload {
    float pdf;
};