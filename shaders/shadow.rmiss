#version 460
#extension GL_EXT_ray_tracing : enable

#include "payload.glsl"
layout(location = 1) rayPayloadInEXT ShadowPayload payload;

void main() {
    payload.shadowRayMiss = true;
}