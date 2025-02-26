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
	bool doubleSided;
};

struct ShaderMaterial {
	vec3 albedo, emissive;
	float alpha, metallic;
	float transmission;
	float attenuationDistance;
	vec3 attenuationColour;
	float ior;
};

layout(binding = 6, set = 0, scalar) readonly buffer Materials { Material materials[]; };