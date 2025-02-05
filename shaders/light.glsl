#ifndef LIGHTS_GLSL
#define LIGHTS_GLSL

#include "maths.glsl"
#include "random.glsl"

struct PointLight {
    vec3 position, colour;
    float intensity, range;
};

struct DirectionalLight {
    vec3 direction, colour;
    float intensity;
};

struct EmissiveSurface {
    uint geometryIdx, baseEmissiveTriangleIdx;
    mat4 transform;
};

struct EmissiveTriangle {
    float pHeuristic;
};

layout(binding = 7, set = 0, scalar) readonly buffer PointLights {
    uint numPointLights;
    PointLight pointLights[];
};
layout(binding = 8, set = 0, scalar) readonly buffer DirectionalLights {
    uint numDirectionalLights;
    DirectionalLight directionalLights[];
};
layout(binding = 9, set = 0, scalar) readonly buffer EmissiveSurfaces {
    uint numEmissiveSurfaces;
    EmissiveSurface emissiveSurfaces[];
};
layout(binding = 10, set = 0, scalar) readonly buffer EmissiveTriangles{
    uint numEmissiveTriangles;
    EmissiveTriangle emissiveTriangles[];
};

#endif