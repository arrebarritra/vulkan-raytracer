#ifndef BSDF_GLSL
#define BSDF_GLSL

#include "maths.glsl"
#include "random.glsl"
#include "spectral.glsl"

vec3 diffuseBRDF(vec3 colour, vec3 L) {
	return float(L.z > 0.0) * colour * PIINV;
}

float D_GGX(vec2 alpha, vec2 anisotropyDirection, vec3 H) {
	float alphaSq = alpha.x * alpha.y;
	float NdotH = H.z;
	float NdotHSq = NdotH * NdotH;
	float HdotAniT = dot(H.xy, anisotropyDirection);
	float HdotAniB = dot(H.xy, anisotropyDirection.yx * vec2(1, -1));

	vec3 f = vec3(alpha.yx, alphaSq) * vec3(HdotAniT, HdotAniB, NdotH);
	float wSq = alphaSq / dot(f, f);
	return alphaSq * wSq * wSq * PIINV;
}

float visibilityFunction(vec2 alpha, vec2 anisotropyDirection, vec3 V, vec3 L) {
	float NdotL = L.z;
	float NdotV = V.z;
	float VdotAniT = dot(V.xy, anisotropyDirection);
	float VdotAniB = dot(V.xy, anisotropyDirection.yx * vec2(1, -1));
	float LdotAniT = dot(L.xy, anisotropyDirection);
	float LdotAniB = dot(L.xy, anisotropyDirection.yx * vec2(1, -1));

	float shadowing = NdotV * length(vec3(alpha.x * LdotAniT, alpha.y * LdotAniB, NdotL));
	float masking = NdotL * length(vec3(alpha.x * VdotAniT, alpha.y * VdotAniB, NdotV));
	return 1 / (2 * (masking + shadowing));
}

float transmissionVisibilityFunction(vec2 alpha, vec2 anisotropyDirection, vec3 V, vec3 L, vec3 H) {
	float HdotL = dot(H, L);
	float HdotV = dot(H, V);

	if (HdotV > 0 && HdotL < 0) { // values might be bad during direct light sampling
		float NdotL = L.z;
		float NdotLSq = NdotL * NdotL;
		float NdotV = V.z;
		float NdotVSq = NdotV * NdotV;
		float VdotAniT = dot(V.xy, anisotropyDirection);
		float VdotAniB = dot(V.xy, anisotropyDirection.yx * vec2(1, -1));
		float LdotAniT = dot(L.xy, anisotropyDirection);
		float LdotAniB = dot(L.xy, anisotropyDirection.yx * vec2(1, -1));

		float shadowing = NdotV * length(vec3(alpha.x * LdotAniT, alpha.y * LdotAniB, NdotL));
		float masking = -NdotL * length(vec3(alpha.x * VdotAniT, alpha.y * VdotAniB, NdotV));
		return 1 / (2 * (masking + shadowing));
	}
	return 0.0;
}

float refractionVisibilityFunction(vec2 alpha, vec2 anisotropyDirection, float eta, vec3 V, vec3 L, vec3 H) {
	float HdotL = dot(H, L);
	float HdotV = dot(H, V);

	if (HdotV > 0 && HdotL < 0) { // values might be bad during direct light sampling
		float NdotL = L.z;
		float NdotLSq = NdotL * NdotL;
		float NdotV = V.z;
		float NdotVSq = NdotV * NdotV;
		float VdotAniT = dot(V.xy, anisotropyDirection);
		float VdotAniB = dot(V.xy, anisotropyDirection.yx * vec2(1, -1));
		float LdotAniT = dot(L.xy, anisotropyDirection);
		float LdotAniB = dot(L.xy, anisotropyDirection.yx * vec2(1, -1));

		float denom = eta * HdotV + HdotL;
		denom *= denom;

		float shadowing = NdotV * length(vec3(alpha.x * LdotAniT, alpha.y * LdotAniB,  NdotL));
		float masking = -NdotL * length(vec3(alpha.x * VdotAniT, alpha.y * VdotAniB, NdotV));
		return 2 * -HdotL * HdotV / (denom * (masking + shadowing));
	}
	return 0.0;
}

float specularBRDF(vec2 alpha, vec2 anisotropyDirection, vec3 V, vec3 L, vec3 H) {
	return visibilityFunction(alpha, anisotropyDirection, V, L) * D_GGX(alpha, anisotropyDirection, H);
}

float specularBTDF(vec2 alpha, vec2 anisotropyDirection, vec3 V, vec3 L, vec3 H) {
	return transmissionVisibilityFunction(alpha, anisotropyDirection, V, L, H) * D_GGX(alpha, anisotropyDirection, H);
}

float refractiveBTDF(vec2 alpha, vec2 anisotropyDirection, float eta, vec3 V, vec3 L, vec3 H) {
	return refractionVisibilityFunction(alpha, anisotropyDirection, eta, V, L, H) * D_GGX(alpha, anisotropyDirection, H);
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

float GGXVNDFSampleReflectionPDF(vec2 alpha, vec2 anisotropyDirection, vec3 view, vec3 halfway) {
	float ndf = D_GGX(alpha, anisotropyDirection, halfway);
	mat2 aniSpaceTransform = mat2(anisotropyDirection, anisotropyDirection.yx * vec2(1, -1));
	vec2 aniSpaceView = aniSpaceTransform * view.xy;
	vec2 ai = alpha * aniSpaceView;	float lenSq = dot(ai, ai);
	float t = sqrt(lenSq + view.z * view.z);

	float s = 1.0 + length(aniSpaceView.xy);
	float a = min(alpha.x, alpha.y);
	float alphaSq = a * a; float sSq = s * s;
	float k = (1.0 - alphaSq) * sSq / (sSq + alphaSq * view.z * view.z);
	return ndf / (2.0 * (k * view.z + t));
}

float GGXVNDFSampleRefractionPDF(vec2 alpha, vec2 anisotropyDirection, float eta, vec3 view, vec3 direction, vec3 halfway) {
	float HdotL = dot(halfway, direction);
	float HdotV = dot(halfway, view);
	float denom = eta * HdotV + HdotL;
	denom *= denom;
	float jacobian = -HdotL / denom;

	float ndf = D_GGX(alpha, anisotropyDirection, halfway);
	mat2 aniSpaceTransform = mat2(anisotropyDirection, anisotropyDirection.yx * vec2(1, -1));
	vec2 aniSpaceView = aniSpaceTransform * view.xy;
	vec2 ai = alpha * aniSpaceView;
	float lenSq = dot(ai, ai);
	float t = sqrt(lenSq + view.z * view.z);

	float s = 1.0 + length(aniSpaceView.xy);
	float a = min(alpha.x, alpha.y);
	float alphaSq = a * a; float sSq = s * s;
	float k = (1.0 - alphaSq) * sSq / (sSq + alphaSq * view.z * view.z);
	return 2.0 * HdotV * ndf / ((k * view.z + t)) * jacobian;
}

// Eto K. and Tokuyoshi Y.: Bounded VNDF Sampling for Smith–GGX Reflections
// https://gpuopen.com/download/publications/Bounded_VNDF_Sampling_for_Smith-GGX_Reflections.pdf
vec3 sampleGGXVNDF(inout uint previous, vec2 alpha, vec2 anisotropyDirection, vec3 view) {
	mat2 aniSpaceTransform = mat2(anisotropyDirection, anisotropyDirection.yx * vec2(1, -1));
	vec2 aniSpaceView = transpose(aniSpaceTransform) * view.xy;
	vec3 viewStd = normalize(vec3(alpha * view.xy, view.z));
	vec2 u = rndSquare(previous);
	float phi = TWOPI * u.x;
	float s = 1.0 + length(view.xy);
	float a = min(alpha.x, alpha.y);
	float alphaSq = a * a; float sSq = s * s;
	float k = (1.0 - alphaSq) * sSq / (sSq + alphaSq * view.z * view.z);
	float b = k * viewStd.z;
	float z = (1.0 - u.y) * (1.0 + b) - b;
	float sinTheta = sqrt(clamp(1.0 - z * z, 0, 1));
	vec3 directionStd = vec3(sinTheta * cos(phi), sinTheta * sin(phi), z);
	// Compute the microfacet normal
	vec3 halfwayStd = viewStd + directionStd;
	vec3 aniSpaceHalfway = normalize(vec3(halfwayStd.xy * alpha, halfwayStd.z));
	return vec3(aniSpaceTransform * aniSpaceHalfway.xy, aniSpaceHalfway.z);
}

float materialPDF(HitInfo hitInfo, vec3 V, vec3 L) {
	HitMaterial hm = hitInfo.hitMat;
	vec3 H;
	float f0Dielectric = (hm.ior - 1) / (hm.ior + 1);
	f0Dielectric *= f0Dielectric;

	float F_transmission;

	float GGXSamplePDF;
	float pTransmission = (1 - hm.metallic) * hm.transmissionFactor;
	float pDiffuse = 0.5 * (1 - hm.metallic);
	float NdotL = L.z;
	if (NdotL < 0) {
		if (hm.thin) {
			H = normalize(V + vec3(L.xy, -L.z));
			float VdotH = dot(V, H);
			F_transmission = fresnelSchlick(f0Dielectric, VdotH);
			GGXSamplePDF = GGXVNDFSampleReflectionPDF(hm.alpha, hm.anisotropyDirection, V, H);
		} else {
			float eta = hitInfo.frontFace ? 1.0 / hm.ior : hm.ior;
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
			GGXSamplePDF = GGXVNDFSampleRefractionPDF(hm.alpha, hm.anisotropyDirection, eta, V, L, H);
		}
		return pTransmission * (1 - F_transmission) * GGXSamplePDF;
	} else {
		vec3 H = normalize(L + V);
		GGXSamplePDF = GGXVNDFSampleReflectionPDF(hm.alpha, hm.anisotropyDirection, V, H);
		float pdf = mix((1 - pTransmission) * GGXSamplePDF, NdotL * PIINV, pDiffuse);
		if (pTransmission > 0) {
			float VdotH = dot(V, H);
			float F_transmission;
			if (hm.thin) {
				F_transmission = fresnelSchlick(f0Dielectric, VdotH);
			} else {
				float eta = hitInfo.frontFace ? 1.0 / hm.ior : hm.ior;
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
}

vec3 materialBSDF(HitInfo hitInfo, float wavelength, vec3 V, vec3 L) {
	HitMaterial hm = hitInfo.hitMat;

	float f0Dielectric = (hm.ior - 1) / (hm.ior + 1);
	f0Dielectric *= f0Dielectric;
	float pTransmission = (1 - hm.metallic) * hm.transmissionFactor;
	float NdotV = V.z;
	float NdotL = L.z;

	float F_dielectric, F_transmission;
	vec3 F_metallic;

	if (hm.dispersion != 0.0) {
		// Wavelength is guaranteed to be collapsed during material sampling
		float wavelengthSq = wavelength * wavelength;
		float B = (hm.ior - 1) * hm.dispersion / (20 * (INV_LAMBDA_F_SQ - INV_LAMBDA_C_SQ));
		float A = hm.ior - B * INV_LAMBDA_D_SQ;
		hm.ior = max(hm.ior + (hm.ior - 1) * hm.dispersion / 20 * (523655 / wavelengthSq - 1.5168), 1);
	}

	vec3 bsdf = vec3(0.0);
	if (NdotL < 0) {
		vec3 H;
		float eta = hitInfo.frontFace ? 1.0 / hm.ior : hm.ior;
		if (hm.thin) {
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

		bsdf = pTransmission * (1 - F_transmission) * hm.baseColour;
		bsdf *= hm.thin ? specularBTDF(hm.alpha, hm.anisotropyDirection, V, L, H) : refractiveBTDF(hm.alpha, hm.anisotropyDirection, eta, V, L, H);
		if (!hm.thin && !hitInfo.frontFace)
			bsdf *= exp(-hm.attenuationCoefficient * hitInfo.t);
		return bsdf;
	} else if (NdotL > 0) {
		vec3 H = normalize(V + L);
		float F_dielectric = fresnelSchlick(f0Dielectric, V, H);
		vec3 F_metallic = fresnelSchlick(hm.baseColour, V, H);

		float specular = specularBRDF(hm.alpha, hm.anisotropyDirection, V, L, H);

		if (pTransmission < 1)
			bsdf += mix(
				mix((1 - hm.transmissionFactor) * diffuseBRDF(hm.baseColour, L), vec3(specular), F_dielectric),
				F_metallic * specular,
				hm.metallic);

		if (pTransmission > 0) {
			float VdotH = dot(V, H);
			float F_transmission;
			if (hm.thin) {
				F_transmission = fresnelSchlick(f0Dielectric, VdotH);
			} else {
				float eta = hitInfo.frontFace ? 1.0 / hm.ior : hm.ior;
				float sinSqThetaOut = eta * eta * (1 - VdotH * VdotH);
				if (eta <= 1.0) {
					F_transmission = fresnelSchlick(f0Dielectric, VdotH);
				} else if (sinSqThetaOut <= 1) {
					F_transmission = fresnelSchlick(f0Dielectric, sqrt(1 - sinSqThetaOut));
				} else {
					F_transmission = 1.0;
				}
			}
			vec3 transmissionBsdf = pTransmission * F_transmission * hm.baseColour * vec3(specular);
			if (!hm.thin && !hitInfo.frontFace)
				transmissionBsdf *= exp(-hm.attenuationCoefficient * hitInfo.t);
			bsdf += transmissionBsdf;
		}
		return bsdf;
	}
	return vec3(0.0);
}

vec3 sampleMaterial(inout uint previous, HitInfo hitInfo, inout float wavelength, vec3 view, out vec3 estimator, out float pdf) {
	HitMaterial hm = hitInfo.hitMat;

	estimator = vec3(0.0);
	vec3 direction = vec3(0.0); vec3 bsdf = vec3(0.0);
	pdf = 0.0;
	float NdotL;
	vec3 halfway;
	float f0Dielectric = (hm.ior - 1) / (hm.ior + 1);
	f0Dielectric *= f0Dielectric;

	float F_dielectric, F_transmission;
	vec3 F_metallic;

	float GGXSamplePDF;
	float pTransmission = (1 - hm.metallic) * hm.transmissionFactor;
	float pDiffuse = 0.5 * (1 - hm.metallic);

	if (hm.dispersion != 0.0) {
		if (wavelength == 0.0) {
			wavelength = rnd(previous, 400.0, 700.0);
			hm.baseColour *= spectralColour1931(wavelength);
		}

		float wavelengthSq = wavelength * wavelength;
		float B = (hm.ior - 1) * hm.dispersion / (20 * (INV_LAMBDA_F_SQ - INV_LAMBDA_C_SQ));
		float A = hm.ior - B * INV_LAMBDA_D_SQ;
		hm.ior = max(hm.ior + (hm.ior - 1) * hm.dispersion / 20 * (523655 / wavelengthSq - 1.5168), 1);
	}

	if (rnd(previous) < pTransmission) {
		halfway = sampleGGXVNDF(previous, hm.alpha, hm.anisotropyDirection, view);
		if (hm.thin) {
			F_transmission = fresnelSchlick(f0Dielectric, view, halfway);
			direction = reflect(-view, halfway);
			if (direction.z < 0) return vec3(0.0);
			
			GGXSamplePDF = GGXVNDFSampleReflectionPDF(hm.alpha, hm.anisotropyDirection, view, halfway);
			if (rnd(previous) > F_transmission)
				direction.z *= -1; // transmission
			NdotL = direction.z;
		} else {
			float eta = hitInfo.frontFace ? 1.0 / hm.ior : hm.ior;
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
				GGXSamplePDF = GGXVNDFSampleReflectionPDF(hm.alpha, hm.anisotropyDirection, view, halfway);
				NdotL = direction.z;
				if (NdotL < 0) return vec3(0.0);
			} else {
				direction = refract(-view, halfway, eta);
				GGXSamplePDF = GGXVNDFSampleRefractionPDF(hm.alpha, hm.anisotropyDirection, eta, view, direction, halfway);
				NdotL = direction.z;
				if (NdotL > 0) return vec3(0.0);
			}
		}

		F_dielectric = fresnelSchlick(f0Dielectric, view, halfway);
		F_metallic = fresnelSchlick(hm.baseColour, view, halfway);
	} else {
		if (rnd(previous) < pDiffuse) {
			direction = sampleCosineHemisphere(previous);
			halfway = normalize(view + direction);
		} else {
			halfway = sampleGGXVNDF(previous, hm.alpha, hm.anisotropyDirection, view);
			direction = reflect(-view, halfway);
		}
		
		NdotL = direction.z;
		if (NdotL < 0) return vec3(0.0);

		GGXSamplePDF = GGXVNDFSampleReflectionPDF(hm.alpha, hm.anisotropyDirection, view, halfway);		
		F_dielectric = fresnelSchlick(f0Dielectric, view, halfway);
		F_metallic = fresnelSchlick(hm.baseColour, view, halfway);

		float eta = hitInfo.frontFace ? 1.0 / hm.ior : hm.ior;
		float VdotH = dot(view, halfway);
		float sinSqThetaOut = eta * eta * (1 - VdotH * VdotH);

		if (hm.thin || eta <= 1.0) {
			F_transmission = fresnelSchlick(f0Dielectric, VdotH);
		} else if (sinSqThetaOut <= 1) {
			F_transmission = fresnelSchlick(f0Dielectric, sqrt(1 - sinSqThetaOut));
		} else {
			F_transmission = 1.0;
		}
	}

	if (NdotL < 0) {
		float eta = hitInfo.frontFace ? 1.0 / hm.ior : hm.ior;
		bsdf = pTransmission * (1 - F_transmission) * hm.baseColour;
		bsdf *= hm.thin ? specularBTDF(hm.alpha, hm.anisotropyDirection, view, direction, halfway) : refractiveBTDF(hm.alpha, hm.anisotropyDirection, eta, view, direction, halfway);
		if (!hm.thin && !hitInfo.frontFace) {
			bsdf *= exp(-hm.attenuationCoefficient * hitInfo.t);
		}

		pdf = pTransmission * (1 - F_transmission) * GGXSamplePDF;
	} else {
		float specular = specularBRDF(hm.alpha, hm.anisotropyDirection, view, direction, halfway);
		
		if (pTransmission < 1) {
			bsdf += mix(
				mix((1 - hm.transmissionFactor) * diffuseBRDF(hm.baseColour, direction), vec3(specular), F_dielectric),
				F_metallic * specular,
				hm.metallic);
			pdf += mix((1 - pTransmission) * GGXSamplePDF, NdotL * PIINV, pDiffuse);
		}

		if (pTransmission > 0) {
			vec3 transmissionBsdf = pTransmission * F_transmission * hm.baseColour * vec3(specular);
			if (!hm.thin && !hitInfo.frontFace)
				transmissionBsdf *= exp(-hm.attenuationCoefficient * hitInfo.t);
			bsdf += transmissionBsdf;
			pdf += pTransmission * F_transmission * GGXSamplePDF;
		}
	}

	estimator = bsdf == vec3(0.0) ? vec3(0.0) : bsdf / pdf * abs(NdotL);
	return direction;
}

#endif