#ifndef RANDOM_GLSL
#define RANDOM_GLSL

/* Copyright (c) 2023, Sascha Willems
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "maths.glsl"

// Tiny Encryption Algorithm
// By Fahad Zafar, Marc Olano and Aaron Curtis, see https://www.highperformancegraphics.org/previous/www_2010/media/GPUAlgorithms/HPG2010_GPUAlgorithms_Zafar.pdf
uint tea(uint val0, uint val1)
{
    uint sum = 0;
    uint v0 = val0;
    uint v1 = val1;
    for (uint n = 0; n < 16; n++)
    {
        sum += 0x9E3779B9;
        v0 += ((v1 << 4) + 0xA341316C) ^ (v1 + sum) ^ ((v1 >> 5) + 0xC8013EA4);
        v1 += ((v0 << 4) + 0xAD90777D) ^ (v0 + sum) ^ ((v0 >> 5) + 0x7E95761E);
    }
    return v0;
}

// Linear congruential generator based on the previous RNG state
// See https://en.wikipedia.org/wiki/Linear_congruential_generator
uint lcg(inout uint previous)
{
    const uint multiplier = 1664525u;
    const uint increment = 1013904223u;
    previous   = (multiplier * previous + increment);
    return previous & 0x00FFFFFF;
}

// Generate a random float in [0, 1) given the previous RNG state
float rnd(inout uint previous)
{
    return (float(lcg(previous)) / float(0x01000000));
}

//------------------------------------------------------------------------

// Generate random float in [min, max] given previous RNG state
float rnd(inout uint previous, float min, float max) {
    return min + rnd(previous) * (max - min);
}

// Generate random int in [min, max] given previous RNG state
int rnd(inout uint previous, int min, int max) {
    return int(lcg(previous) % (max - min + 1) + min);
}

// Generate random uint in [min, max] given previous RNG state
uint rnd(inout uint previous, uint min, uint max) {
    return uint(lcg(previous) % (max - min + 1) + min);
}

// Uniformly random point in square
vec2 rndSquare(inout uint previous) {
    return vec2(rnd(previous), rnd(previous));
}

// Uniformly random point in cube
vec3 rndCube(inout uint previous) {
    return vec3(rnd(previous), rnd(previous), rnd(previous));
}

// Uniformly random point on hemisphere with normal z+
vec3 sampleUniformHemisphere(inout uint previous) {
    vec2 u = rndSquare(previous);
    float r = sqrt(1 - u.x * u.x);
    return vec3(r * vec2(cos(TWOPI * u.y), sin(TWOPI * u.y)), u.x);
}

// Uniformly random point on normal oriented hemisphere
vec3 sampleUniformHemisphere(inout uint previous, vec3 normal) {
    vec2 u = rndSquare(previous);
    float r = sqrt(1 - u.x * u.x);
    vec3 p = vec3(r * vec2(cos(TWOPI * u.y), sin(TWOPI * u.y)), u.x);
    return dot(p, normal) >= 0 ? p : -p;
}

// Cosine distributed point on hemisphere with normal z+
vec3 sampleCosineHemisphere(inout uint previous) {
    vec2 u = rndSquare(previous);
    float r = u.x;
    vec3 p;
    p.xy = r * vec2(sin(TWOPI * u.y), cos(TWOPI * u.y));
    p.z = 1 - dot(p.xy, p.xy);
    return p;
}

// Cosine distributed point on normal oriented hemisphere
vec3 sampleCosineHemisphere(inout uint previous, vec3 normal) {
    vec3 tangent, bitangent;
    branchlessONB(normal, tangent, bitangent);
    
    vec3 p;
    vec2 u = rndSquare(previous);
    float r = u.x;
    p.xy = r * vec2(sin(TWOPI * u.y), cos(TWOPI * u.y));
    p.z = 1 - dot(p.xy, p.xy);
    return p.x * tangent + p.y * bitangent + p.z * normal;
}

#endif