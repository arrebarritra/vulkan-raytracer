struct Material {
	vec4 baseColourFactor;
	int alphaMode;
	float alphaCutoff;
	vec3 emissiveFactor;
	float roughnessFactor, metallicFactor;
	float emissiveStrength;
	float transmissionFactor;
	float thicknessFactor, attenuationDistance;
	vec3 attenuationColour;
	float ior;
	int baseColourTexIdx, metallicRoughnessTexIdx, normalTexIdx, emissiveTexIdx, transmissionTexIdx;
	bool doubleSided;
};

layout(binding = 6, set = 0, scalar) readonly buffer Materials { Material materials[]; };