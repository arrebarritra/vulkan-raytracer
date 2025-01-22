#pragma once
#include <glm/glm.hpp>

namespace vkrt {

enum class LightTypes {
	Point, Directional
};

struct PointLight {
	glm::vec3 position, colour;
	float intensity, range;
};

struct DirectionalLight {
	glm::vec3 direction, colour;
	float intensity;
};

struct EmissiveSurface {
	uint32_t geometryIdx, baseEmissiveTriangleIdx;
	glm::mat4 transform;
};

struct EmissiveTriangle {
	float pHeuristic;
};

}