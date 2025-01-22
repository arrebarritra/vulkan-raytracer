#version 460
#extension GL_EXT_ray_tracing : enable

#include "payload.glsl"
layout(location = 0) rayPayloadInEXT RayPayload payload;

void main() {
    payload.emittedLight = 15.0 * vec3(0.6, 0.8, 0.95);
    payload.lightSample = vec3(0.0);
    payload.scatter = false;
}