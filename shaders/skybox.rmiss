#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_scalar_block_layout : require

layout(binding = 4, set = 0, scalar) uniform PathTracingProperties{
    uint sampleCount, maxRayDepth;
    float skyboxStrength;
} pathTracing;
layout(binding = 11, set = 0) uniform sampler2D skyboxTexture;

#include "payload.glsl"
#include "maths.glsl"
layout(location = 0) rayPayloadInEXT RayPayload payload;

const vec2 invAtan = vec2(TWOPIINV, PIINV);
vec2 dirToUV(vec3 v) {
    vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
    uv *= invAtan;
    uv += 0.5;
    uv.y *= -1;
    return uv;
}

void main() {
    vec2 uv = dirToUV(gl_WorldRayDirectionEXT);
    payload.emittedLight = pathTracing.skyboxStrength * texture(skyboxTexture, uv).rgb;
    payload.lightSample = vec3(0.0);
    payload.scatter = false;
}