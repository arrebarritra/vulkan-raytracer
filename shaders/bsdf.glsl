#ifndef BSDF_GLSL
#define BSDF_GLSL

#include "maths.glsl"
#include "random.glsl"

vec3 diffuseBRDF(vec3 colour, vec3 N, vec3 L) {
	return float(dot(N, L) > 0.0) * colour * PIINV;
}

float D_GGX(float alpha, vec3 N, vec3 H) {
	alpha = max(0.001, alpha);
	float alphaSq = alpha * alpha;
	float NdotH = dot(N, H);
	float NdotHSq = NdotH * NdotH;

	if (NdotH > 0.0) {
		float num = alphaSq * PIINV;
		float denom = NdotHSq * (alphaSq - 1) + 1;
		denom = denom * denom;
		return num / denom;
	} else {
		return 0.0;
	}
}

float G_1(float alpha, vec3 N, vec3 S) {
	float alphaSq = alpha * alpha;
	float NdotS = dot(N, S);
	float NdotSSq = NdotS * NdotS;

	return 1 / (NdotS  + sqrt(mix(NdotSSq, 1, alphaSq)));
}

float G_2(float alpha, vec3 N, vec3 V, vec3 L) {
	float alphaSq = alpha * alpha;
	float NdotL = dot(N, L);
	float NdotLSq = NdotL * NdotL;
	float NdotV = dot(N, V);
	float NdotVSq = NdotV * NdotV;

	float shadowing = NdotV + sqrt(mix(NdotLSq, 1, alphaSq));
	float masking = NdotL + sqrt(mix(NdotVSq, 1, alphaSq));
	return 1 / (masking + shadowing);
}

float visibilityFunction(float alpha, vec3 N, vec3 V, vec3 L) {
	float alphaSq = alpha * alpha;
	float NdotL = dot(N, L);
	float NdotLSq = NdotL * NdotL;
	float NdotV = dot(N, V);
	float NdotVSq = NdotV * NdotV;

	if (NdotV > 0 && NdotL > 0) {
		float shadowing = NdotV + sqrt(mix(NdotLSq, 1, alphaSq));
		float masking = NdotL + sqrt(mix(NdotVSq, 1, alphaSq));
		return 1 / (masking + shadowing);
	} else {
		return 0.0;
	}
}

float specularBRDF(float alpha, vec3 N, vec3 V, vec3 L, vec3 H) {
	return visibilityFunction(alpha, N, V, L) * D_GGX(alpha, N, H);
}

vec3 fresnelSchlick(vec3 f0, vec3 V, vec3 H) {
	float VdotH = dot(V, H);
	vec3 F = mix(vec3(pow(1 - VdotH, 5)), vec3(1), f0);
	return mix(vec3(pow(1 - VdotH, 5)), vec3(1), f0);
}

vec3 sampleDiffuse(inout uint previous, vec3 normal) {
	return sampleCosineHemisphere(previous, normal);
}

// Based on Heitz (2017): "A Simpler and Exact Sampling Routine for the GGX Distribution of Visible Normals"
// https://hal.science/hal-01509746/document
vec3 sampleGGXVNDF(inout uint previous, float alpha, vec3 normal, vec3 view) {
	vec3 tangent, bitangent;
	branchlessONB(normal, tangent, bitangent);
	mat3 TBN = mat3(tangent, bitangent, normal);
	vec3 tangentView = transpose(TBN) * view;

	// stretch view
	vec3 v = normalize(vec3(alpha * tangentView.xy, tangentView.z));
	// normal space view orthonormal basis
	float lensq = dot(v.xy, v.xy);
	vec3 t1 = lensq > 0 ? vec3(-v.y, v.x, 0) * inversesqrt(lensq) : vec3(1, 0, 0);
	vec3 t2 = cross(v, t1);

	// sample point with polar coordinates (r, phi)
	vec2 u = rndSquare(previous);
	float r = sqrt(u.x);
	float phi = TWOPI * u.y;
	float p1 = r * cos(phi);
	float p2 = r * sin(phi);
	float s = 0.5 * (1.0 + v.z);
	p2 = (1.0 - s) * sqrt(1.0 - p1 * p1) + s * p2;
	// compute normal
	vec3 tangentMicrofacetNormal = p1 * t1 + p2 * t2 + sqrt(max(0.0, 1.0 - p1 * p1 - p2 * p2)) * v;
	// unstretch
	tangentMicrofacetNormal = normalize(vec3(alpha * tangentMicrofacetNormal.xy, max(0.0, tangentMicrofacetNormal.z)));
	return TBN * tangentMicrofacetNormal;
}

float materialPDF(vec3 baseColour, float metallic, float roughness, float ior, vec3 N, vec3 V, vec3 L, vec3 H) {
	float alpha = roughness * roughness;
	float pDiffuse = min(0.75 * (1 - metallic), 0.5 * roughness + 0.25);
	float NdotV = dot(N, V);
	float NdotL = dot(N, L);

	return mix(D_GGX(alpha, N, H) * G_1(alpha, N, V) / (4 * NdotV), NdotL * PIINV, pDiffuse);
}

vec3 materialBSDF(vec3 baseColour, float metallic, float roughness, float ior, vec3 N, vec3 V, vec3 L, vec3 H) {
	vec3 diffuseColour = mix(baseColour, vec3(0.0), metallic);
	float f0Dielectric = (ior - 1) / (ior + 1);
	f0Dielectric = f0Dielectric * f0Dielectric;
	vec3 specularColour = mix(vec3(f0Dielectric), baseColour, metallic);
	float alpha = roughness * roughness;

	vec3 F = fresnelSchlick(specularColour, V, H);
	return mix(diffuseBRDF(diffuseColour, N, L), vec3(specularBRDF(alpha, N, V, L, H)), F);
}

vec3 sampleMaterial(inout uint previous, vec3 baseColour, float metallic, float roughness, float ior, vec3 normal, vec3 view) {
	vec3 diffuseColour = mix(baseColour, vec3(0.0), metallic);
	float alpha = roughness * roughness;
	float f0Dielectric = (ior - 1) / (ior + 1);
	f0Dielectric = f0Dielectric * f0Dielectric;
	vec3 specularColour = mix(vec3(f0Dielectric), baseColour, metallic);

	vec3 direction, halfway;
	float pDiffuse = min(0.75 * (1 - metallic), 0.5 * roughness + 0.25);
	bool diffuse = rnd(previous) < pDiffuse;
	if (diffuse) {
		return sampleDiffuse(previous, normal);
	} else {
		halfway = sampleGGXVNDF(previous, alpha, normal, view);
		return reflect(-view, halfway);
	}
}

#endif