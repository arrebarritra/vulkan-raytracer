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

// Generate random int in [min, max] given previous RNG state
int rnd(inout uint previous, int min, int max) {
    return int(min + rnd(previous) * (max - min + 1));
}

// Generate random uint in [min, max] given previous RNG state
uint rnd(inout uint previous, uint min, uint max) {
    return uint(min + rnd(previous) * (max - min + 1u));
}

// Uniformly random point in square
vec2 rndSquare(inout uint previous) {
    return vec2(rnd(previous), rnd(previous));
}

// Uniformly random point in cube
vec3 rndCube(inout uint previous) {
    return vec3(rnd(previous), rnd(previous), rnd(previous));
}

// Building an Orthonormal Basis, Revisited
// Tom Duff et. al: https://graphics.pixar.com/library/OrthonormalB/paper.pdf
void branchlessONB(const vec3 n, out vec3 tangent, out vec3 bitangent)
{
    float sign = n.z > 0.0 ? 1.0 : -1.0;
    const float a = -1.0f / (sign + n.z);
    const float b = n.x * n.y * a;
    tangent = vec3(1.0f + sign * POW2(n.x) * a, sign * b, -sign * n.x);
    bitangent = vec3(b, sign + POW2(n.y) * a, -n.y);
}

// Uniformly random point on normal oriented hemisphere
vec3 sampleUniformHemisphere(vec3 normal, inout uint previous) {
    vec2 u = rndSquare(previous);
    float r = sqrt(1 - POW2(u.x));
    vec3 p = vec3(r * vec2(cos(TWOPI * u.y), sin(TWOPI * u.y)), u.x);
    return sign(dot(p, normal)) * p;
}

// Uniformly random point on normal oriented hemisphere
vec3 sampleCosineHemisphere(vec3 normal, inout uint previous) {
    vec3 tangent, bitangent;
    branchlessONB(normal, tangent, bitangent);
    
    vec2 u = rndSquare(previous);
    float r = 1 - u.x;
    return sqrt(u.x) * normal + r * sin(TWOPI * u.y) * bitangent + r * cos(TWOPI * u.y) * tangent;
}

#endif