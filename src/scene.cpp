#include <scene.h>

namespace vkrt {

SceneObject::SceneObject(SceneObject* parent, glm::mat4& transform, std::optional<uint32_t> meshIdx)
	: transform(transform), parent(parent), meshIdx(meshIdx), depth(parent ? parent->depth + 1u : 0u) {}

Scene::Scene() : root(nullptr, glm::mat4(1.0f), std::nullopt), objectCount(0u), maxDepth(1u) {}

SceneObject& Scene::addObject(SceneObject* parent, glm::mat4& transform, std::optional<uint32_t> meshIdx) {
	objectCount++;
	auto& so = parent->children.emplace_back(parent ? parent : &root, transform, meshIdx);
	maxDepth = std::max(maxDepth, so.depth);
	return so;
}

}
