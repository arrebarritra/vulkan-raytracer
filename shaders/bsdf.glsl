#ifndef BSDF_GLSL
#define BSDF_GLSL

#include "maths.glsl"
#include "random.glsl"

vec3 diffuseBRDF(vec3 colour, vec3 L) {
	return float(L.z > 0.0) * colour * PIINV;
}

float D_GGX(float alpha, vec3 H) {
	alpha = max(0.001, alpha);
	float alphaSq = alpha * alpha;
	float NdotH = H.z;
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

float G_1(float alpha, vec3 S) {
	float alphaSq = alpha * alpha;
	float NdotS = S.z;
	float NdotSSq = NdotS * NdotS;
	 
	return 1 / (NdotS  + sqrt(mix(NdotSSq, 1, alphaSq)));
}

float visibilityFunction(float alpha, vec3 V, vec3 L) {
	float alphaSq = alpha * alpha;
	float NdotL = L.z;
	float NdotLSq = NdotL * NdotL;
	float NdotV = V.z;
	float NdotVSq = NdotV * NdotV;

	if (NdotV > 0 && NdotL > 0) {
		float shadowing = NdotV + sqrt(mix(NdotLSq, 1, alphaSq));
		float masking = NdotL + sqrt(mix(NdotVSq, 1, alphaSq));
		return 1 / (masking + shadowing);
	} else {
		return 0.0;
	}
}

float specularBRDF(float alpha, vec3 V, vec3 L, vec3 H) {
	return visibilityFunction(alpha, V, L) * D_GGX(alpha, H);
}

float fresnelSchlick(float f0, vec3 V, vec3 H) {
	float VdotH = abs(dot(V, H));
	return mix(pow(1 - VdotH, 5), 1, f0);
}

vec3 fresnelSchlick(vec3 f0, vec3 V, vec3 H) {
	float VdotH = abs(dot(V, H));
	return mix(vec3(pow(1 - VdotH, 5)), vec3(1), f0);
}

float GGXVNDFSamplePDF(float alpha, vec3 view, vec3 halfway) {
	float ndf = D_GGX(alpha, halfway);
	vec2 ai = alpha * view.xy;
	float lenSq = dot(ai, ai);
	float t = sqrt(lenSq + view.z * view.z);
	if (view.z >= 0.0) {
		float s = 1.0 + length(view.xy); // Omit sgn for a <=1
		float alphaSq = alpha * alpha; float sSq = s * s;
		float k = (1.0 - alphaSq) * sSq / (sSq + alphaSq * view.z * view.z);
		return ndf / (2.0 * (k * view.z + t));
	}
	// Numerically stable form of the previous PDF for view.z < 0
	return ndf * (t - view.z) / (2.0 * lenSq);

}

// Eto K. and Tokuyoshi Y.: Bounded VNDF Sampling for Smith–GGX Reflections
// https://gpuopen.com/download/publications/Bounded_VNDF_Sampling_for_Smith-GGX_Reflections.pdf
vec3 sampleGGXVNDF(inout uint previous, float alpha, vec3 view) {
	vec3 viewStd = normalize(vec3(alpha * view.xy, view.z));
	vec2 u = rndSquare(previous);
	float phi = TWOPI * u.x;
	float s = 1.0 + length(view.xy);
	float alphaSq = alpha * alpha; float sSq = s * s;
	float k = (1.0 - alphaSq) * sSq / (sSq + alphaSq * view.z * view.z);
	float b = view.z > 0 ? k * viewStd.z : viewStd.z;
	float z = (1.0 - u.y) * (1.0 + b) - b;
	float sinTheta = sqrt(clamp(1.0 - z * z, 0, 1));
	vec3 directionStd = vec3(sinTheta * cos(phi), sinTheta * sin(phi), z);
	// Compute the microfacet normal
	vec3 halfwayStd = viewStd + directionStd;
	return normalize(vec3(halfwayStd.xy * alpha, halfwayStd.z));
}

float materialPDF(vec3 baseColour, float metallic, float roughness, float transmissionFactor, float ior, vec3 V, vec3 L) {
	float alpha = roughness * roughness;
	float eta = (ior - 1) / (ior + 1);
	eta *= eta;
	float pTransmission = (1 - metallic) * transmissionFactor;

	float NdotL = L.z;
	if (pTransmission > 0 && NdotL < 0) {
		vec3 H = normalize(V + vec3(L.xy, -L.z));
		float specularPDF = GGXVNDFSamplePDF(alpha, V, H);
		return pTransmission * specularPDF;
	} else if (NdotL > 0) {
		float pDiffuse = 0.5 * (1 - metallic);
		vec3 H = normalize(L + V);
		float F = fresnelSchlick(eta, V, H);
		float specularPDF = GGXVNDFSamplePDF(alpha, V, H);
		return mix((1 - pTransmission) * specularPDF, max(0.0, NdotL) * PIINV, pDiffuse);
	}
	return 0.0;
}

vec3 materialBSDF(vec3 baseColour, float metallic, float roughness, float transmissionFactor, float ior, vec3 V, vec3 L) {
	float alpha = roughness * roughness;
	float eta = (ior - 1) / (ior + 1);
	eta *= eta;
	float pTransmission = (1 - metallic) * transmissionFactor;
	float NdotV = V.z;
	float NdotL = L.z;

	if (pTransmission > 0 && NdotL < 0) {
		vec3 HT = normalize(V + vec3(L.xy, -L.z));
		float F = fresnelSchlick(eta, V, HT);
		return pTransmission * (1 - F) * baseColour * specularBRDF(alpha, V, vec3(L.xy, -L.z), HT) * -NdotL;
	} else if (pTransmission < 1 && NdotL > 0) {
		vec3 H = normalize(V + L);
		float F_dielectric = fresnelSchlick(eta, V, H);
		vec3 F_metallic = fresnelSchlick(baseColour, V, H);
		float specular = specularBRDF(alpha, V, L, H);
		return mix(mix((1 - transmissionFactor) * diffuseBRDF(baseColour, L), vec3(specular), F_dielectric),
					F_metallic * specular,
					metallic) * NdotL;
	}
	return vec3(0.0);
}

vec3 sampleMaterial(inout uint previous, vec3 baseColour, float metallic, float roughness, float transmissionFactor, float ior, vec3 view, out vec3 estimator) {
	vec3 direction = vec3(0.0); vec3 bsdf = vec3(0.0); float pdf = 0.0;
	float NdotL;
	vec3 halfway;
	float alpha = roughness * roughness;
	float eta = (ior - 1) / (ior + 1);
	eta *= eta;

	float pTransmission = (1 - metallic) * transmissionFactor;
	if (rnd(previous) < pTransmission) {
		halfway = sampleGGXVNDF(previous, alpha, view);
		direction = reflect(-view, halfway);
		direction.z *= -1;
		NdotL = direction.z;
		float F = fresnelSchlick(eta, view, halfway);

		pdf = pTransmission * GGXVNDFSamplePDF(alpha, view, halfway);
		bsdf = NdotL < 0 ? pTransmission * (1 - F) * baseColour * specularBRDF(alpha, view, vec3(direction.xy, -direction.z), halfway) : vec3(0.0);
	} else {
		float pDiffuse = 0.5 * (1 - metallic);
		if (rnd(previous) < pDiffuse) {
			direction = sampleCosineHemisphere(previous);
			halfway = normalize(view + direction);
		} else {
			halfway = sampleGGXVNDF(previous, alpha, view);
			direction = reflect(-view, halfway);
		}

		NdotL = direction.z;
		float specularPDF = GGXVNDFSamplePDF(alpha, view, halfway);
		pdf = mix((1 - pTransmission) * specularPDF, max(0.0, NdotL) * PIINV, pDiffuse);
		
		float F_dielectric = fresnelSchlick(eta, view, halfway);
		vec3 F_metallic = fresnelSchlick(baseColour, view, halfway);
		float specular = specularBRDF(alpha, view, direction, halfway);
		bsdf = NdotL > 0 ? mix(
				   mix((1 - transmissionFactor) * diffuseBRDF(baseColour, direction), vec3(specular), F_dielectric),
				   F_metallic * specular,
				   metallic) * NdotL : vec3(0.0);
	}
	estimator = bsdf == vec3(0.0) ? vec3(0.0) : bsdf / pdf * abs(NdotL);
	if (any(isinf(estimator)) || any(isnan(estimator)) || any(lessThan(estimator, vec3(0.0)))) debugPrintfEXT("Estimator: (%v3f)\n", estimator);
	return direction;
}

#endif