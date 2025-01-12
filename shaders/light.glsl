struct PointLight {
    vec3 position, colour;
    float intensity, range;
};

struct DirectionalLight {
    vec3 direction, colour;
    float intensity;
};

struct EmissiveSurface {
    uint geometryIdx;
    vec3 minCoord, maxCoord;
    mat4 transform;
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