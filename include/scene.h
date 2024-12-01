#pragma once

#include <iterator>
#include <mesh.h>
#include <glm/glm.hpp>
#include <unordered_set>

namespace vkrt {

class Scene;
class SceneObject {
	friend class Scene;

public:
	SceneObject(SceneObject* parent, glm::mat4& transform, std::optional<uint32_t> meshIdx);
	SceneObject(const SceneObject&) = delete;
	SceneObject& operator=(const SceneObject&) = delete;

	glm::mat4 transform;
	std::optional<int> meshIdx;

private:
	SceneObject* parent;
	std::list<SceneObject> children;
};

class Scene {

public:
	Scene();

	SceneObject root;
	uint32_t maxDepth;
	uint32_t objectCount;
	std::vector<Mesh> meshPool;

	void addObject(SceneObject* parent = nullptr, glm::mat4& transform = glm::mat4(1.0f), std::optional<uint32_t> meshIdx = std::nullopt);


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
			if (root) transformAtDepth[0] = root->transform;
		};
		reference operator*() const { return depth == 0u ? *root : *nodeAtDepth[depth - 1u]; }
		pointer operator->() { return depth == 0 ? root : &(*nodeAtDepth[depth - 1u]); }

		iterator& operator++() {
			if ((**this).children.size() > 0) {
				nodeAtDepth[depth] = (**this).children.begin();
				transformAtDepth[depth + 1u] = (**this).children.front().transform * transformAtDepth[depth];
				depth++;
			} else {
				for (; depth > 0 && (**this).parent->children.end() == ++nodeAtDepth[depth - 1u]; depth--) {}
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
		std::vector<std::list<value_type>::iterator> nodeAtDepth; // "stack"
		std::vector<glm::mat4> transformAtDepth; // keep track of transforms instead of inverting
	};

	iterator begin() { return iterator(&root, maxDepth); };
	iterator end() { return iterator(nullptr, 0); };
};

}