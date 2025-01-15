#ifndef BSDF_GLSL
#define BSDF_GLSL

#include "maths.glsl"
#include "random.glsl"

vec3 diffuseBRDF(vec3 colour) {
	return colour * PIINV;
}

float D_GGX(float alpha, vec3 N, vec3 H) {
	float alphaSq = alpha * alpha;
	float NdotH = dot(N, H);
	float NdotHSq = NdotH * NdotH;

	float num = alphaSq * PIINV;
	float denom = NdotHSq * (alphaSq - 1) + 1;
	denom = denom * denom;
	return num / denom;
}

float visibilityFunction(float alpha, vec3 N, vec3 V, vec3 L) {
	float alphaSq = alpha * alpha;
	float NdotL = dot(N, L);
	float NdotLSq = NdotL * NdotL;
	float NdotV = dot(N, V);
	float NdotVSq = NdotV * NdotV;

	float shadowing = NdotV + sqrt(mix(NdotLSq, 1, alphaSq));
	float masking = NdotL + sqrt(mix(NdotVSq, 1, alphaSq));
	return 1 / (masking + shadowing);
}

float specularBRDF(float alpha, vec3 N, vec3 V, vec3 L, vec3 H) {
	return visibilityFunction(alpha, N, V, L) * D_GGX(alpha, N, H);
}

vec3 fresnelSchlick(vec3 f0, vec3 V, vec3 H) {
	float VdotH = dot(V, H);
	return mix(vec3(pow(1 - VdotH, 5)), vec3(1), f0);
}

vec3 materialBSDF(vec3 baseColour, float metallic, float roughness, float ior, vec3 N, vec3 V, vec3 L, vec3 H) {
	vec3 diffuseColour = mix(baseColour, vec3(0.0), metallic);
	float f0Dielectric = (ior - 1) / (ior + 1);
	f0Dielectric = f0Dielectric * f0Dielectric;
	vec3 f0 = mix(vec3(f0Dielectric), baseColour, metallic);
	float alpha = roughness * roughness;

	vec3 F = fresnelSchlick(f0, V, H);
	return mix(diffuseBRDF(diffuseColour), vec3(specularBRDF(alpha, N, V, L, H)), F);
}

#endif