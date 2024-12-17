#pragma once

namespace vkrt {

struct Material {
	glm::vec4 baseColourFactor = glm::vec4(0.0f);
	float roughnessFactor = 0.0f, metallicFactor = 0.0f;
	int baseColourTexIdx = -1, roughnessMetallicTexIdx = -1, normalTexIdx = -1;
	bool doubleSided = false; // GLSL bools 4 bytes
};

}