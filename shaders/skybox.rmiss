#version 460
#extension GL_EXT_ray_tracing : enable

#include "payload.glsl"
layout(location = 0) rayPayloadInEXT PathTracingPayload payload;

void main() {
    payload.hitNormal = vec3(-1.0);
    payload.baseColour = vec3(0.0);
    payload.emittedLight = 0.0 * vec3(0.6, 0.8, 0.95);
    payload.directLight = vec3(0.0);
}