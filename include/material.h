#pragma once

namespace vkrt {

struct Material {
	glm::vec4 baseColourFactor = glm::vec4(1.0f);
	int alphaMode = 0; 
	float alphaCutoff = 0.5;
	glm::vec3 emissiveFactor = glm::vec3(0.0f); // emissive strength is pre-multiplied
	float roughnessFactor = 1.0f, metallicFactor = 1.0f;
	float transmissionFactor = 0.0f;
	float thicknessFactor = 0.0f;
	glm::vec3 attenuationCoefficient = glm::vec3(0.0f);
	float ior = 1.5;
	int baseColourTexIdx = -1, metallicRoughnessTexIdx = -1, normalTexIdx = -1, emissiveTexIdx = -1, transmissionTexIdx = -1;
	bool doubleSided = false; // GLSL bools 4 bytes
};

}