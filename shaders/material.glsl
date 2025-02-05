struct Material {
	vec4 baseColourFactor;
	int alphaMode;
	float alphaCutoff;
	vec3 emissiveFactor;
	float roughnessFactor, metallicFactor;
	float transmissionFactor;
	float thicknessFactor, attenuationDistance;
	vec3 attenuationColour;
	float ior;
	int baseColourTexIdx, metallicRoughnessTexIdx, normalTexIdx, emissiveTexIdx, transmissionTexIdx;
	bool doubleSided;
};

struct ShaderMaterial {
	vec3 albedo, emissive;
	float roughness, metallic;
	float transmission;
	float attenuationDistance;
	vec3 attenuationColour;
	float ior;
};

layout(binding = 6, set = 0, scalar) readonly buffer Materials { Material materials[]; };