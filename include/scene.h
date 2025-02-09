#pragma once

#include <iterator>
#include <filesystem>

#include <glm/glm.hpp>
#include <camera.h>

#include <mesh.h>
#include <material.h>
#include <texture.h>
#include <light.h>

#define TINYGLTF_NO_EXTERNAL_IMAGE
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include <tiny_gltf.h>

namespace vkrt {

class Scene;
class SceneObject {
	friend class Scene;

public:
	SceneObject(SceneObject* parent, glm::mat4& localTransform = glm::mat4(1.0f), int meshIdx = -1);
	SceneObject(const SceneObject&) = delete;
	SceneObject& operator=(const SceneObject&) = delete;

	glm::mat4 localTransform, worldTransform;
	int meshIdx;

private:
	uint32_t depth;
	SceneObject* parent;
	std::list<SceneObject> children;
};

class Scene {

public:
	Scene(vk::SharedDevice device, DeviceMemoryManager& dmm, ResourceTransferHandler& rth);

	SceneObject root;
	uint32_t maxDepth;
	uint32_t objectCount;

	// Resources
	std::vector<Mesh> meshPool;
	std::vector<GeometryInfo> geometryInfos;
	std::vector<Material> materials;
	std::vector<std::unique_ptr<Texture>> texturePool;
	std::unordered_map<std::filesystem::path, uint32_t> texturesNameToIndex;
	std::vector<PointLight> pointLights;
	std::vector<DirectionalLight> directionalLights;
	std::vector<EmissiveSurface> emissiveSurfaces;
	std::vector<EmissiveTriangle> emissiveTriangles;
	std::vector<std::tuple<LightTypes, uint32_t>> lightGlobalToTypeIndex;

	std::unique_ptr<Buffer> geometryInfoBuffer, materialsBuffer, pointLightsBuffer, directionalLightsBuffer, emissiveSurfacesBuffer, emissiveTrianglesBuffer;

	SceneObject& addNode(SceneObject* parent, glm::mat4& localTransform = glm::mat4(1.0f), int meshIdx = -1);
	void loadModel(std::filesystem::path path, SceneObject* parent, glm::mat4& localTransform = glm::mat4(1.0f));
	void uploadResources();

	// Stackless iterator
	class iterator {

	public:
		using iterator_category = std::forward_iterator_tag;
		using difference_type = std::ptrdiff_t;
		using value_type = SceneObject;
		using pointer = SceneObject*;
		using reference = SceneObject&;

		iterator(const pointer root, uint32_t maxDepth)
			: root(root)
			, depth(0u)
			, nodeAtDepth(std::vector<std::list<SceneObject>::iterator>(maxDepth)) {};
#define nodeDepth depth - 1u
		reference operator*() const { return depth == 0u ? *root : *nodeAtDepth[nodeDepth]; }
		pointer operator->() { return depth == 0 ? root : &(*nodeAtDepth[nodeDepth]); }

		iterator& operator++() {
			if ((**this).children.size() > 0) {
				nodeAtDepth[nodeDepth + 1u] = (**this).children.begin();
				depth++;
			} else {
				for (; depth > 0 && (**this).parent->children.end() == ++nodeAtDepth[nodeDepth]; depth--) {}
				if (depth == 0) {
					root = nullptr;
					return iterator(nullptr, 0);
				}// end
			}
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
	};

	iterator begin() { return iterator(&root, maxDepth); };
	iterator end() { return iterator(nullptr, 0); };

private:
	void processModelRecursive(SceneObject* parent, const tinygltf::Model& model, const tinygltf::Node& node, uint32_t baseObjectCount);
	void processEmissivePrimitive(const tinygltf::Model& model, const tinygltf::Primitive& primitive, const glm::mat4 localTransform);

	vk::SharedDevice device;
	DeviceMemoryManager& dmm;
	ResourceTransferHandler& rth;
};

}