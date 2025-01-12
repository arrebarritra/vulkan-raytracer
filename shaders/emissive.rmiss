#version 460
#extension GL_EXT_ray_tracing : enable

#include "payload.glsl"
layout(location = 2) rayPayloadInEXT EmissivePayload payload;

void main() {
    payload.instanceHit = false;
}