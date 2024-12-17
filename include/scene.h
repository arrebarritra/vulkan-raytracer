#pragma once

#include <iterator>
#include <filesystem>

#include <glm/glm.hpp>
#include <assimp/scene.h>
#include <camera.h>

#include <mesh.h>
#include <material.h>
#include <texture.h>
#include <light.h>

namespace vkrt {

class Scene;
class SceneObject {
	friend class Scene;

public:
	SceneObject(SceneObject* parent, glm::mat4& transform, std::vector<uint32_t> meshIndices);
	SceneObject(const SceneObject&) = delete;
	SceneObject& operator=(const SceneObject&) = delete;

	glm::mat4 transform;
	std::vector<uint32_t> meshIndices;

private:
	uint32_t depth;
	SceneObject* parent;
	std::list<SceneObject> children;
};

class Scene {

public:
	Scene(vk::SharedDevice device, DeviceMemoryManager& dmm, ResourceCopyHandler& rch);

	SceneObject root;
	uint32_t maxDepth;
	uint32_t objectCount;

	// Resources
	std::vector<Mesh> meshPool;
	std::vector<Material> materials;
	std::vector<std::unique_ptr<Texture>> texturePool;
	std::unordered_map<std::filesystem::path, uint32_t> texturesNameToIndex;
	std::vector<PointLight> pointLights;
	std::vector<DirectionalLight> directionalLights;
	std::unordered_map<std::string, std::tuple<LightTypes, uint32_t>> lightNameToIndex;

	std::unique_ptr<Buffer> geometryInfoBuffer, materialsBuffer, pointLightsBuffer, directionalLightsBuffer;

	SceneObject& addObject(SceneObject* parent, glm::mat4& transform, std::vector<uint32_t> meshIndices);
	void loadModel(SceneObject* parent, glm::mat4& transform, std::filesystem::path path);


	// Stackless iterator
	class iterator {

	public:
		using iterator_category = std::forward_iterator_tag;
		using difference_type = std::ptrdiff_t;
		using value_type = SceneObject;
		using pointer = SceneObject*;
		using reference = SceneObject&;

		// Global model matrix
		glm::mat4 transform;

		iterator(const pointer root, uint32_t maxDepth)
			: root(root)
			, transform(root ? root->transform : glm::mat4(0.0f))
			, depth(0u)
			, nodeAtDepth(std::vector<std::list<SceneObject>::iterator>(maxDepth))
			, transformAtDepth(std::vector<glm::mat4>(maxDepth + 1)) {
			if (root) { transformAtDepth[0] = root->transform; }
		};
#define nodeDepth depth - 1u
		reference operator*() const { return depth == 0u ? *root : *nodeAtDepth[nodeDepth]; }
		pointer operator->() { return depth == 0 ? root : &(*nodeAtDepth[nodeDepth]); }

		iterator& operator++() {
			if ((**this).children.size() > 0) {
				nodeAtDepth[nodeDepth + 1u] = (**this).children.begin();
				transformAtDepth[depth + 1u] = (**this).children.front().transform * transformAtDepth[depth];
				depth++;
			} else {
				for (; depth > 0 && (**this).parent->children.end() == ++nodeAtDepth[nodeDepth]; depth--) {}
				if (depth == 0) {
					root = nullptr;
					return iterator(nullptr, 0);
				}// end
				transformAtDepth[depth] = (**this).transform * transformAtDepth[depth - 1u];
			}
			transform = transformAtDepth[depth];
			return *this;
		}
		iterator operator++(int) { iterator temp = *this; ++(*this); return temp; }

		friend bool operator==(const iterator& a, const iterator& b) {
			return a.root == b.root && &(*a) == &(*b); // Ensure not only same object but same iterator (same root guarantees same sequence of iteration)
		}
		friend bool operator!=(const iterator& a, const iterator& b) { return !(a == b); }


	private:
		pointer root;
		uint32_t depth;
		std::vector<std::list<value_type>::iterator> nodeAtDepth; // "Stack"
		std::vector<glm::mat4> transformAtDepth; // Keep track of transforms instead of inverting
	};

	iterator begin() { return iterator(&root, maxDepth); };
	iterator end() { return iterator(nullptr, 0); };

private:
	void processModelRecursive(SceneObject* parent, const aiScene* scene, const aiNode* node, const glm::mat4 parentTransform, uint32_t baseMeshOffset);

	vk::SharedDevice device;
	DeviceMemoryManager& dmm;
	ResourceCopyHandler& rch;
};

}