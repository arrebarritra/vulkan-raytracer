#ifndef MATERIAL_GLSL
#define MATERIAL_GLSL

struct Material {
	vec4 baseColourFactor;
	int alphaMode;
	float alphaCutoff;
	vec3 emissiveFactor;
	float metallicFactor, roughnessFactor;
	float transmissionFactor;
	float thicknessFactor;
	vec3 attenuationCoefficient;
	float ior;
	float anisotropyStrength, anisotropyRotation;
	float dispersion;
	int baseColourTexIdx, metallicRoughnessTexIdx, normalTexIdx, emissiveTexIdx, transmissionTexIdx, anisotropyTexIdx;
};

layout(binding = 6, set = 0, scalar) readonly buffer Materials { Material materials[]; };

#endif