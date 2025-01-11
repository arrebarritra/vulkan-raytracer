struct PointLight {
    vec3 position, colour;
    float intensity, range;
};

struct DirectionalLight {
    vec3 direction, colour;
    float intensity;
};

layout(binding = 7, set = 0, scalar) readonly buffer PointLights {
    uint numPointLights;
    PointLight pointLights[];
};
layout(binding = 8, set = 0, scalar) readonly buffer DirectionalLights {
    uint numDirectionalLights;
    DirectionalLight directionalLights[];
};