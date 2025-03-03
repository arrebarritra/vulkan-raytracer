#ifndef PAYLOAD_GLSL
#define PAYLOAD_GLSL

#include "hit.glsl"

struct RayPayload {
    uint seed;
    HitInfo hitInfo;
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

#endif