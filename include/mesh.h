#pragma once

#include <glm/glm.hpp>
#include <buffer.h>
#include <optional>

namespace vkrt {

struct Vertex {
	glm::vec3 position, normal, tangent, bitangent;
	glm::vec2 uv0, uv1;
};

using Index = uint32_t;

struct MeshInfo {
	vk::DeviceAddress vertexBufferAddress, indexBufferAddress;
	uint32_t materialIdx;

	MeshInfo(vk::DeviceAddress vertexBufferAddress, vk::DeviceAddress indexBufferAddress, uint32_t materialIdx)
		: vertexBufferAddress(vertexBufferAddress), indexBufferAddress(indexBufferAddress), materialIdx(materialIdx) {}
};

class Mesh {
public:
	Mesh(vk::SharedDevice device, DeviceMemoryManager& dmm, ResourceCopyHandler& rch, vk::ArrayProxyNoTemporaries<Vertex> vertices, vk::ArrayProxyNoTemporaries<Index> indices, uint32_t materialIdx = -1u);

	uint32_t nVertices, nIndices, materialIdx;
	std::unique_ptr<Buffer> vertices, indices;

};

}