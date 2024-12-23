#pragma once

namespace vkrt {

struct Material {
	glm::vec4 baseColourFactor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
	glm::vec3 emissiveFactor = glm::vec3(0.0f);
	float roughnessFactor = 0.0f, metallicFactor = 0.0f;
	int baseColourTexIdx = -1, metallicRoughnessTexIdx = -1, normalTexIdx = -1, emissiveTexIdx = -1;
	bool doubleSided = false; // GLSL bools 4 bytes
};

}