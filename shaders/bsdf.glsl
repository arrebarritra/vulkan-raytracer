#ifndef BSDF_GLSL
#define BSDF_GLSL

#include "maths.glsl"
#include "random.glsl"

vec3 diffuseBRDF(vec3 colour, vec3 L) {
	return float(L.z > 0.0) * colour * PIINV;
}

float D_GGX(float alpha, vec3 H) {
	float alphaSq = alpha * alpha;
	float NdotH = H.z;
	float NdotHSq = NdotH * NdotH;

	float num = alphaSq * PIINV;
	float denom = NdotHSq * (alphaSq - 1) + 1;
	denom = denom * denom;
	return num / denom;
}

float visibilityFunction(float alpha, vec3 V, vec3 L) {
	float alphaSq = alpha * alpha;
	float NdotL = L.z;
	float NdotLSq = NdotL * NdotL;
	float NdotV = V.z;
	float NdotVSq = NdotV * NdotV;

	float shadowing = NdotV * sqrt(mix(NdotLSq, 1, alphaSq));
	float masking = NdotL * sqrt(mix(NdotVSq, 1, alphaSq));
	return 1 / (2 * (masking + shadowing));
}

float transmissionVisibilityFunction(float alpha, vec3 V, vec3 L, vec3 H) {
	float alphaSq = alpha * alpha;
	float NdotL = L.z;
	float NdotLSq = NdotL * NdotL;
	float NdotV = V.z;
	float NdotVSq = NdotV * NdotV;
	float HdotL = dot(H, L);
	float HdotV = dot(H, V);

	if (HdotV > 0 && HdotL < 0) { // values might be bad during direct light sampling
		float shadowing = NdotV * sqrt(mix(NdotLSq, 1, alphaSq));
		float masking = -NdotL * sqrt(mix(NdotVSq, 1, alphaSq));
		return 1 / (2 * (masking + shadowing));
	}
	return 0.0;
}

float refractionVisibilityFunction(float alpha, float eta, vec3 V, vec3 L, vec3 H) {
	float alphaSq = alpha * alpha;
	float NdotL = L.z;
	float NdotLSq = NdotL * NdotL;
	float NdotV = V.z;
	float NdotVSq = NdotV * NdotV;
	float HdotL = dot(H, L);
	float HdotV = dot(H, V);
	float denom = eta * HdotV + HdotL;
	denom *= denom;

	if (HdotV > 0 && HdotL < 0) { // values might be bad during direct light sampling
		float shadowing = NdotV * sqrt(mix(NdotLSq, 1, alphaSq));
		float masking = -NdotL * sqrt(mix(NdotVSq, 1, alphaSq));
		return 2 * -HdotL * HdotV / (denom * (masking + shadowing));
	}
	return 0.0;
}

float specularBRDF(float alpha, vec3 V, vec3 L, vec3 H) {
	return visibilityFunction(alpha, V, L) * D_GGX(alpha, H);
}

float specularBTDF(float alpha, vec3 V, vec3 L, vec3 H) {
	return transmissionVisibilityFunction(alpha, V, L, H) * D_GGX(alpha, H);
}

float refractiveBTDF(float alpha, float eta, vec3 V, vec3 L, vec3 H) {
	return refractionVisibilityFunction(alpha, eta, V, L, H) * D_GGX(alpha, H);
}

float fresnelSchlick(float f0, float costheta) {
	return mix(pow(1 - costheta, 5), 1, f0);
}

vec3 fresnelSchlick(vec3 f0, float costheta) {
	return mix(vec3(pow(1 - costheta, 5)), vec3(1), f0);
}

float fresnelSchlick(float f0, vec3 V, vec3 H) {
	float VdotH = abs(dot(V, H));
	return mix(pow(1 - VdotH, 5), 1, f0);
}

vec3 fresnelSchlick(vec3 f0, vec3 V, vec3 H) {
	float VdotH = abs(dot(V, H));
	return mix(vec3(pow(1 - VdotH, 5)), vec3(1), f0);
}

float GGXVNDFSampleReflectionPDF(float alpha, vec3 view, vec3 halfway) {
	float ndf = D_GGX(alpha, halfway);
	vec2 ai = alpha * view.xy;
	float lenSq = dot(ai, ai);
	float t = sqrt(lenSq + view.z * view.z);

	float s = 1.0 + length(view.xy);
	float alphaSq = alpha * alpha; float sSq = s * s;
	float k = (1.0 - alphaSq) * sSq / (sSq + alphaSq * view.z * view.z);
	return ndf / (2.0 * (k * view.z + t));
}

float GGXVNDFSampleRefractionPDF(float alpha, float eta, vec3 view, vec3 direction, vec3 halfway) {
	float HdotL = dot(halfway, direction);
	float HdotV = dot(halfway, view);
	float denom = eta * HdotV + HdotL;
	denom *= denom;
	float jacobian = -HdotL / denom;

	float ndf = D_GGX(alpha, halfway);
	vec2 ai = alpha * view.xy;
	float lenSq = dot(ai, ai);
	float t = sqrt(lenSq + view.z * view.z);

	float s = 1.0 + length(view.xy);
	float alphaSq = alpha * alpha; float sSq = s * s;
	float k = (1.0 - alphaSq) * sSq / (sSq + alphaSq * view.z * view.z);
	return 2.0 * HdotV * ndf / ((k * view.z + t)) * jacobian;
}

// Eto K. and Tokuyoshi Y.: Bounded VNDF Sampling for Smith�GGX Reflections
// https://gpuopen.com/download/publications/Bounded_VNDF_Sampling_for_Smith-GGX_Reflections.pdf
vec3 sampleGGXVNDF(inout uint previous, float alpha, vec3 view) {
	vec3 viewStd = normalize(vec3(alpha * view.xy, view.z));
	vec2 u = rndSquare(previous);
	float phi = TWOPI * u.x;
	float s = 1.0 + length(view.xy);
	float alphaSq = alpha * alpha; float sSq = s * s;
	float k = (1.0 - alphaSq) * sSq / (sSq + alphaSq * view.z * view.z);
	float b = k * viewStd.z;
	float z = (1.0 - u.y) * (1.0 + b) - b;
	float sinTheta = sqrt(clamp(1.0 - z * z, 0, 1));
	vec3 directionStd = vec3(sinTheta * cos(phi), sinTheta * sin(phi), z);
	// Compute the microfacet normal
	vec3 halfwayStd = viewStd + directionStd;
	return normalize(vec3(halfwayStd.xy * alpha, halfwayStd.z));
}

float materialPDF(vec3 baseColour, float metallic, float roughness, float transmissionFactor, float ior, bool thin, bool ffnormal, vec3 V, vec3 L) {
	vec3 H;
	float alpha = roughness * roughness;
	alpha = max(0.001, alpha);
	float f0Dielectric = (ior - 1) / (ior + 1);
	f0Dielectric *= f0Dielectric;

	float F_transmission;

	float GGXSamplePDF;
	float pTransmission = (1 - metallic) * transmissionFactor;
	float pDiffuse = 0.5 * (1 - metallic);
	float NdotL = L.z;
	if (NdotL < 0) {
		if (thin) {
			H = normalize(V + vec3(L.xy, -L.z));
			float VdotH = dot(V, H);
			F_transmission = fresnelSchlick(f0Dielectric, VdotH);
			GGXSamplePDF = GGXVNDFSampleReflectionPDF(alpha, V, H);
		} else {
			float eta = ffnormal ? 1.0 / ior : ior;
			H = eta > 1.0 ? normalize(eta * V + L) : -normalize(eta * V + L);
			float VdotH = dot(V, H);
			float sinSqThetaOut = eta * eta * (1 - VdotH * VdotH);
			if (eta <= 1.0) {
				F_transmission = fresnelSchlick(f0Dielectric, VdotH);
			} else if (sinSqThetaOut <= 1) {
				F_transmission = fresnelSchlick(f0Dielectric, sqrt(1 - sinSqThetaOut));
			} else {
				F_transmission = 1.0;
			}
			GGXSamplePDF = GGXVNDFSampleRefractionPDF(alpha, eta, V, L, H);
		}
		return pTransmission * (1 - F_transmission) * GGXSamplePDF;
	} else if (NdotL > 0) {
		vec3 H = normalize(L + V);
		GGXSamplePDF = GGXVNDFSampleReflectionPDF(alpha, V, H);
		float pdf = mix((1 - pTransmission) * GGXSamplePDF, NdotL * PIINV, pDiffuse);
		if (pTransmission > 0) {
			float VdotH = dot(V, H);
			float F_transmission;
			if (thin) {
				F_transmission = fresnelSchlick(f0Dielectric, VdotH);
			} else {
				float eta = ffnormal ? 1.0 / ior : ior;
				float sinSqThetaOut = eta * eta * (1 - VdotH * VdotH);
				if (eta <= 1.0) {
					F_transmission = fresnelSchlick(f0Dielectric, VdotH);
				} else if (sinSqThetaOut <= 1) {
					F_transmission = fresnelSchlick(f0Dielectric, sqrt(1 - sinSqThetaOut));
				} else {
					F_transmission = 1.0;
				}
			}
			pdf += pTransmission * F_transmission * GGXSamplePDF;
		}
		return pdf;
	}
	return 0.0;
}

vec3 materialBSDF(vec3 baseColour, float metallic, float roughness, float transmissionFactor, float ior, bool thin, vec3 attenuationCoefficient, bool ffnormal, vec3 V, vec3 L) {
	float alpha = roughness * roughness;
	alpha = max(0.001, alpha);
	float f0Dielectric = (ior - 1) / (ior + 1);
	f0Dielectric *= f0Dielectric;
	float pTransmission = (1 - metallic) * transmissionFactor;
	float NdotV = V.z;
	float NdotL = L.z;

	float F_dielectric, F_transmission;
	vec3 F_metallic;
	if (NdotL < 0) {
		vec3 H;
		float eta = ffnormal ? 1.0 / ior : ior;
		if (thin) {
			H = normalize(V + vec3(L.xy, -L.z));
			float F_transmission = fresnelSchlick(f0Dielectric, V, H);
		} else {
			H = eta > 1.0 ? normalize(eta * V + L) : -normalize(eta * V + L);
			float VdotH = dot(V, H);
			float sinSqThetaOut = eta * eta * (1 - VdotH * VdotH);
			if (eta <= 1.0) {
				F_transmission = fresnelSchlick(f0Dielectric, VdotH);
			} else if (sinSqThetaOut <= 1) {
				F_transmission = fresnelSchlick(f0Dielectric, sqrt(1 - sinSqThetaOut));
			} else {
				F_transmission = 1.0;
			}
		}

		vec3 bsdf = pTransmission * (1 - F_transmission) * baseColour;
		bsdf *= thin ? specularBTDF(alpha, V, L, H) : refractiveBTDF(alpha, eta, V, L, H);
		if (!thin && !ffnormal)
			bsdf *= exp(-attenuationCoefficient * gl_HitTEXT);
		return bsdf;
	} else if (NdotL > 0) {
		vec3 H = normalize(V + L);
		float F_dielectric = fresnelSchlick(f0Dielectric, V, H);
		vec3 F_metallic = fresnelSchlick(baseColour, V, H);

		float specular = specularBRDF(alpha, V, L, H);
		vec3 bsdf = mix(
			mix((1 - transmissionFactor) * diffuseBRDF(baseColour, L), vec3(specular), F_dielectric),
			F_metallic * specular,
			metallic);

		if (pTransmission > 0) {
			float VdotH = dot(V, H);
			float F_transmission;
			if (thin) {
				F_transmission = fresnelSchlick(f0Dielectric, VdotH);
			} else {
				float eta = ffnormal ? 1.0 / ior : ior;
				float sinSqThetaOut = eta * eta * (1 - VdotH * VdotH);
				if (eta <= 1.0) {
					F_transmission = fresnelSchlick(f0Dielectric, VdotH);
				} else if (sinSqThetaOut <= 1) {
					F_transmission = fresnelSchlick(f0Dielectric, sqrt(1 - sinSqThetaOut));
				} else {
					F_transmission = 1.0;
				}
			}
			vec3 transmissionBsdf = pTransmission * F_transmission * baseColour * vec3(specular);
			if (!thin && !ffnormal)
				transmissionBsdf *= exp(-attenuationCoefficient * gl_HitTEXT);
			bsdf += transmissionBsdf;
		}
		return bsdf;
	}
	return vec3(0.0);
}

vec3 sampleMaterial(inout uint previous, vec3 baseColour, float metallic, float roughness, float transmissionFactor, float ior, bool thin, vec3 attenuationCoefficient, bool ffnormal, vec3 view, out vec3 estimator, out float pdf) {
	estimator = vec3(0.0);
	vec3 direction = vec3(0.0); vec3 bsdf = vec3(0.0);
	pdf = 0.0;
	float NdotL;
	vec3 halfway;
	float alpha = roughness * roughness;
	alpha = max(0.001, alpha);
	float f0Dielectric = (ior - 1) / (ior + 1);
	f0Dielectric *= f0Dielectric;

	float F_dielectric, F_transmission;
	vec3 F_metallic;

	float GGXSamplePDF;
	float pTransmission = (1 - metallic) * transmissionFactor;
	float pDiffuse = 0.5 * (1 - metallic);
	if (rnd(previous) < pTransmission) {
		halfway = sampleGGXVNDF(previous, alpha, view);
		if (thin) {
			F_transmission = fresnelSchlick(f0Dielectric, view, halfway);
			direction = reflect(-view, halfway);
			if (direction.z < 0) {
				estimator = vec3(0.0);
				return vec3(0.0);
			}
			
			GGXSamplePDF = GGXVNDFSampleReflectionPDF(alpha, view, halfway);
			if (rnd(previous) > F_transmission)
				direction.z *= -1; // transmission
			NdotL = direction.z;
		} else {
			float eta = ffnormal ? 1.0 / ior : ior;
			float VdotH = dot(view, halfway);
			float sinSqThetaOut = eta * eta * (1 - VdotH * VdotH);
			
			if (eta <= 1.0) {
				F_transmission = fresnelSchlick(f0Dielectric, VdotH);	
			} else if (sinSqThetaOut <= 1) {
				F_transmission = fresnelSchlick(f0Dielectric, sqrt(1 - sinSqThetaOut));
			} else {
				F_transmission = 1.0;
			}

			if (rnd(previous) < F_transmission) {
				direction = reflect(-view, halfway);
				GGXSamplePDF = GGXVNDFSampleReflectionPDF(alpha, view, halfway);
				NdotL = direction.z;
				if (NdotL < 0) return vec3(0.0);
			} else {
				direction = refract(-view, halfway, eta);
				GGXSamplePDF = GGXVNDFSampleRefractionPDF(alpha, eta, view, direction, halfway);
				NdotL = direction.z;
				if (NdotL > 0) return vec3(0.0);
			}
		}

		F_dielectric = fresnelSchlick(f0Dielectric, view, halfway);
		F_metallic = fresnelSchlick(baseColour, view, halfway);
	} else {
		if (rnd(previous) < pDiffuse) {
			direction = sampleCosineHemisphere(previous);
			halfway = normalize(view + direction);
		} else {
			halfway = sampleGGXVNDF(previous, alpha, view);
			direction = reflect(-view, halfway);
		}
		
		NdotL = direction.z;
		if (NdotL < 0) return vec3(0.0);

		GGXSamplePDF = GGXVNDFSampleReflectionPDF(alpha, view, halfway);		
		F_dielectric = fresnelSchlick(f0Dielectric, view, halfway);
		F_metallic = fresnelSchlick(baseColour, view, halfway);

		float eta = ffnormal ? 1.0 / ior : ior;
		float VdotH = dot(view, halfway);
		float sinSqThetaOut = eta * eta * (1 - VdotH * VdotH);

		if (thin || eta <= 1.0) {
			F_transmission = fresnelSchlick(f0Dielectric, VdotH);
		} else if (sinSqThetaOut <= 1) {
			F_transmission = fresnelSchlick(f0Dielectric, sqrt(1 - sinSqThetaOut));
		} else {
			F_transmission = 1.0;
		}
	}

	if (NdotL < 0) {
		float eta = ffnormal ? 1.0 / ior : ior;
		bsdf = pTransmission * (1 - F_transmission) * baseColour;
		bsdf *= thin ? specularBTDF(alpha, view, direction, halfway) : refractiveBTDF(alpha, eta, view, direction, halfway);
		if (!thin && !ffnormal) {
			bsdf *= exp(-attenuationCoefficient * gl_HitTEXT);
		}

		pdf = pTransmission * (1 - F_transmission) * GGXSamplePDF;
	} else {
		float specular = specularBRDF(alpha, view, direction, halfway);
		bsdf = mix(
			mix((1 - transmissionFactor) * diffuseBRDF(baseColour, direction), vec3(specular), F_dielectric),
			F_metallic * specular,
			metallic);
		pdf = mix((1 - pTransmission) * GGXSamplePDF, NdotL * PIINV, pDiffuse);
		if (pTransmission > 0) {
			vec3 transmissionBsdf = pTransmission * F_transmission * baseColour * vec3(specular);
			if (!thin && !ffnormal)
				transmissionBsdf *= exp(-attenuationCoefficient * gl_HitTEXT);
			bsdf += transmissionBsdf;
			pdf += pTransmission * F_transmission * GGXSamplePDF;
		}
	}
	estimator = bsdf == vec3(0.0) ? vec3(0.0) : bsdf / pdf * abs(NdotL);
	return direction;
}

#endif