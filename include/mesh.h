#pragma once

#include <glm/glm.hpp>
#include <buffer.h>
#include <optional>

namespace vkrt {

struct Vertex {
	glm::vec3 position, normal;
	glm::vec4 tangent;
	glm::vec2 uv0, uv1;
};

using Index = uint32_t;

struct GeometryInfo {
	vk::DeviceAddress vertexBufferAddress, indexBufferAddress;
	uint32_t materialIdx;

	GeometryInfo(vk::DeviceAddress vertexBufferAddress, vk::DeviceAddress indexBufferAddress, uint32_t materialIdx)
		: vertexBufferAddress(vertexBufferAddress), indexBufferAddress(indexBufferAddress), materialIdx(materialIdx) {}
};

class Mesh {
public:
	Mesh(vk::SharedDevice device, DeviceMemoryManager& dmm, ResourceTransferHandler& rth, std::vector<std::vector<Vertex>> vertices, std::vector<std::vector<Index>> indices, std::vector<uint32_t> materialIndices);

	std::vector<uint32_t> vertexCounts, indexCounts, materialIndices;
	std::vector<std::unique_ptr<Buffer>> vertexBuffers, indexBuffers;
};

}