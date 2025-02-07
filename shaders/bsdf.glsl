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

float refractiveVisibilityFunction(float alpha, float ior_i, float ior_o, vec3 V, vec3 L, vec3 H) {
	float alphaSq = alpha * alpha;
	float NdotL = L.z;
	float NdotLSq = NdotL * NdotL;
	float NdotV = V.z;
	float NdotVSq = NdotV * NdotV;
	float HdotL = abs(dot(H, L));
	float HdotV = abs(dot(H, V));
	float denom = ior_i * HdotV + ior_o * HdotL;
	denom *= denom;

	if (NdotV > 0 && NdotL > 0) {
		float shadowing = NdotV + sqrt(mix(NdotLSq, 1, alphaSq));
		float masking = NdotL + sqrt(mix(NdotVSq, 1, alphaSq));
		return 4 * HdotL * HdotV * ior_o * ior_o / ((masking + shadowing) * denom);
	} else {
		return 0.0;
	}
}

float specularBRDF(float alpha, vec3 V, vec3 L, vec3 H) {
	return visibilityFunction(alpha, V, L) * D_GGX(alpha, H);
}

float refractiveBTDF(float alpha, float ior_i, float ior_o, vec3 V, vec3 L, vec3 H) {
	return refractiveVisibilityFunction(alpha, ior_i, ior_o, V, H, L) * D_GGX(alpha, H);
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

float materialPDF(vec3 baseColour, float metallic, float roughness, float transmissionFactor, float ior, bool thin, float normalSign, vec3 V, vec3 L) {
	vec3 H;
	float alpha = roughness * roughness;
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
		} else {
			float ior_i = normalSign == 1.0 ? 1.0 : ior;
			float ior_o = normalSign == 1.0 ? ior : 1.0;
			float eta = ior_i / ior_o;

			H = normalize(ior_i * V + ior_o * vec3(L.xy, -L.z));
			float VdotH = dot(V, H);
			float sinSqTheta_o = eta * eta * (1 - VdotH * VdotH);
			if (ior_o >= ior_i) {
				F_transmission = fresnelSchlick(f0Dielectric, VdotH);
			} else if (ior_o < ior_i && sinSqTheta_o <= 1) {
				F_transmission = fresnelSchlick(f0Dielectric, 1 - sqrt(1 - sinSqTheta_o));
			} else {
				F_transmission = 1.0;
			}
		}

		GGXSamplePDF = GGXVNDFSamplePDF(alpha, V, H);
		return pTransmission * (1 - F_transmission) * GGXSamplePDF;
	} else if (NdotL > 0) {
		vec3 H = normalize(L + V);
		GGXSamplePDF = GGXVNDFSamplePDF(alpha, V, H);
		float pdf = mix((1 - pTransmission) * GGXSamplePDF, max(0.0, NdotL) * PIINV, pDiffuse);

		if (pTransmission > 0) {
			float VdotH = dot(V, H);
			float F_transmission;
			if (thin) {
				F_transmission = fresnelSchlick(f0Dielectric, VdotH);
			} else {
				float ior_i = normalSign == 1.0 ? 1.0 : ior;
				float ior_o = normalSign == 1.0 ? ior : 1.0;
				float eta = ior_i / ior_o;

				float sinSqTheta_o = eta * eta * (1 - VdotH * VdotH);
				if (ior_o >= ior_i) {
					F_transmission = fresnelSchlick(f0Dielectric, VdotH);
				} else if (ior_o < ior_i && sinSqTheta_o <= 1) {
					F_transmission = fresnelSchlick(f0Dielectric, 1 - sqrt(1 - sinSqTheta_o));
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

vec3 materialBSDF(vec3 baseColour, float metallic, float roughness, float transmissionFactor, float ior, bool thin, float attenuationDistance, vec3 attenuationColour, float normalSign, vec3 V, vec3 L) {
	float alpha = roughness * roughness;
	float f0Dielectric = (ior - 1) / (ior + 1);
	f0Dielectric *= f0Dielectric;
	float pTransmission = (1 - metallic) * transmissionFactor;
	float NdotV = V.z;
	float NdotL = L.z;

	float F_dielectric, F_transmission;
	vec3 F_metallic;
	float ior_i, ior_o;
	if (NdotL < 0) {
		vec3 H;
		if (thin) {
			H = normalize(V + vec3(L.xy, -L.z));
			float F_transmission = fresnelSchlick(f0Dielectric, V, H);
		} else {
			ior_i = normalSign == 1.0 ? 1.0 : ior;
			ior_o = normalSign == 1.0 ? ior : 1.0;
			float eta = ior_i / ior_o;

			H = normalize(ior_i * V + ior_o * vec3(L.xy, -L.z));
			float VdotH = dot(V, H);
			float sinSqTheta_o = eta * eta * (1 - VdotH * VdotH);
			if (ior_o >= ior_i) {
				F_transmission = fresnelSchlick(f0Dielectric, VdotH);
			} else if (ior_o < ior_i && sinSqTheta_o <= 1) {
				F_transmission = fresnelSchlick(f0Dielectric, 1 - sqrt(1 - sinSqTheta_o));
			} else {
				F_transmission = 1.0;
			}
		}

		vec3 bsdf = pTransmission * (1 - F_transmission) * baseColour;
		bsdf *= thin ? specularBRDF(alpha, V, vec3(L.xy, -L.z), H) : refractiveBTDF(alpha, ior_i, ior_o, V, vec3(L.xy, -L.z), H);
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
				ior_i = normalSign == 1.0 ? 1.0 : ior;
				ior_o = normalSign == 1.0 ? ior : 1.0;
				float eta = ior_i / ior_o;

				float sinSqTheta_o = eta * eta * (1 - VdotH * VdotH);
				if (ior_o >= ior_i) {
					F_transmission = fresnelSchlick(f0Dielectric, VdotH);
				} else if (ior_o < ior_i && sinSqTheta_o <= 1) {
					F_transmission = fresnelSchlick(f0Dielectric, 1 - sqrt(1 - sinSqTheta_o));
				} else {
					F_transmission = 1.0;
				}
			}
			bsdf += pTransmission * F_transmission * baseColour * vec3(specular);
			if (!thin && normalSign == -1.0)
				bsdf *= pow(attenuationColour, vec3(gl_HitTEXT / attenuationDistance));
		}
		return bsdf;
	}
	return vec3(0.0);
}

vec3 sampleMaterial(inout uint previous, vec3 baseColour, float metallic, float roughness, float transmissionFactor, float ior, bool thin, float attenuationDistance, vec3 attenuationColour, float normalSign, vec3 view, out vec3 estimator, out float pdf) {
	estimator = vec3(0.0);
	vec3 direction = vec3(0.0); vec3 bsdf = vec3(0.0);
	pdf = 0.0;
	float NdotL;
	vec3 halfway;
	float alpha = roughness * roughness;
	float f0Dielectric = (ior - 1) / (ior + 1);
	f0Dielectric *= f0Dielectric;

	float F_dielectric, F_transmission;
	vec3 F_metallic;
	float ior_i, ior_o;

	float GGXSamplePDF;
	float pTransmission = (1 - metallic) * transmissionFactor;
	float pDiffuse = 0.5 * (1 - metallic);
	if (rnd(previous) < pTransmission) {
		halfway = sampleGGXVNDF(previous, alpha, view);
		GGXSamplePDF = GGXVNDFSamplePDF(alpha, view, halfway);

		F_dielectric = fresnelSchlick(f0Dielectric, view, halfway);
		F_metallic = fresnelSchlick(baseColour, view, halfway);
		if (thin) {
			F_transmission = fresnelSchlick(f0Dielectric, view, halfway);
			direction = reflect(-view, halfway);
			if (direction.z < 0) {
				estimator = vec3(0.0);
				return vec3(0.0);
			}
			
			if (rnd(previous) > F_transmission)
				direction.z *= -1; // transmission
			NdotL = direction.z;
		} else {
			ior_i = normalSign == 1.0 ? 1.0 : ior;
			ior_o = normalSign == 1.0 ? ior : 1.0;
			float eta = ior_i / ior_o;
			float VdotH = dot(view, halfway);
			float sinSqTheta_o = eta * eta * (1 - VdotH * VdotH);
			
			if (ior_o >= ior_i) {
				F_transmission = fresnelSchlick(f0Dielectric, VdotH);	
			} else if (ior_o < ior_i && sinSqTheta_o <= 1) {
				F_transmission = fresnelSchlick(f0Dielectric, 1 - sqrt(1 - sinSqTheta_o));
			} else {
				F_transmission = 1.0;
			}

			if (rnd(previous) < F_transmission) {
				direction = reflect(-view, halfway);
				NdotL = direction.z;
				if (NdotL < 0) return vec3(0.0);
			} else {
				direction = refract(-view, halfway, eta);
				NdotL = direction.z;
				if (NdotL > 0) return vec3(0.0);
			}
		}
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

		GGXSamplePDF = GGXVNDFSamplePDF(alpha, view, halfway);		
		F_dielectric = fresnelSchlick(f0Dielectric, view, halfway);
		F_metallic = fresnelSchlick(baseColour, view, halfway);

		ior_i = normalSign == 1.0 ? 1.0 : ior;
		ior_o = normalSign == 1.0 ? ior : 1.0;
		float eta = ior_i / ior_o;
		float VdotH = dot(view, halfway);
		float sinSqTheta_o = eta * eta * (1 - VdotH * VdotH);

		if (ior_o >= ior_i) {
			F_transmission = fresnelSchlick(f0Dielectric, VdotH);
		} else if (ior_o < ior_i && sinSqTheta_o <= 1) {
			F_transmission = fresnelSchlick(f0Dielectric, 1 - sqrt(1 - sinSqTheta_o));
		} else {
			F_transmission = 1.0;
		}
	}

	if (NdotL < 0) {
		bsdf = pTransmission * (1 - F_transmission) * baseColour;
		bsdf *= thin ? specularBRDF(alpha, view, vec3(direction.xy, -direction.z), halfway) : refractiveBTDF(alpha, ior_i, ior_o, view, vec3(direction.xy, -direction.z), halfway);
		if (!thin && normalSign == -1.0)
			bsdf *= pow(attenuationColour, vec3(gl_HitTEXT / attenuationDistance));
		pdf = pTransmission * (1 - F_transmission) * GGXSamplePDF;
	} else {
		float specular = specularBRDF(alpha, view, direction, halfway);
		bsdf = mix(
			mix((1 - transmissionFactor) * diffuseBRDF(baseColour, direction), vec3(specular), F_dielectric),
			F_metallic * specular,
			metallic) 
			+ pTransmission * F_transmission * baseColour * vec3(specular);
		pdf = mix((1 - pTransmission) * GGXSamplePDF, NdotL * PIINV, pDiffuse)
			+ pTransmission * F_transmission * GGXSamplePDF;
	}
	estimator = bsdf == vec3(0.0) ? vec3(0.0) : bsdf / pdf * abs(NdotL);
	return direction;
}

#endif