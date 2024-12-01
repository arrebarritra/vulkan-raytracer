#pragma once

#include <glm/glm.hpp>
#include <buffer.h>
#include <optional>

namespace vkrt {

class MeshKey {
public:
	bool transparent;
	uint32_t nVertices, nIndices;
	vk::Buffer vertices, indices;

	bool operator==(const MeshKey& m) {
		return transparent == m.transparent && nVertices == m.nVertices && nIndices == m.nIndices
			&& vertices == m.vertices && indices == m.indices;
	}
};

class Mesh {
public:
	Mesh(vk::SharedDevice device, DeviceMemoryManager& dmm, ResourceCopyHandler& rch, vk::ArrayProxyNoTemporaries<glm::vec3> vertices, vk::ArrayProxyNoTemporaries<uint32_t> indices);

	bool transparent;
	uint32_t nVertices, nIndices;
	std::unique_ptr<Buffer> vertices, indices;
	//std::optional<Buffer> vertexColours, vertexNormals, vertexUV;

	MeshKey getMeshKey() {
		return MeshKey{ transparent, nVertices, nIndices, **vertices, **indices };
	};

};

}

template<>
struct std::hash<vkrt::MeshKey> {
	size_t operator()(const vkrt::MeshKey& m) const {
		return std::hash<uint64_t>{}(reinterpret_cast<uint64_t>(&m.vertices)) ^ std::hash<uint64_t>{}(reinterpret_cast<uint64_t>(&m.indices));
	}
};