#include <mesh.h>

namespace vkrt {

Mesh::Mesh(vk::SharedDevice device, DeviceMemoryManager& dmm, ResourceTransferHandler& rth, std::vector<std::vector<Vertex>> primitiveVertices, std::vector<std::vector<Index>> primitiveIndices, std::vector<uint32_t> materialIndices)
	: materialIndices(materialIndices)
{
	assert(primitiveVertices.size() == primitiveIndices.size() && primitiveIndices.size() == materialIndices.size());
	vertexCounts.reserve(primitiveVertices.size());
	indexCounts.reserve(primitiveIndices.size());
	vertexBuffers.reserve(primitiveVertices.size());
	indexBuffers.reserve(primitiveIndices.size());

	std::transform(primitiveVertices.begin(), primitiveVertices.end(), std::back_inserter(vertexCounts), [&](std::vector<Vertex>& vertices) { return static_cast<uint32_t>(vertices.size()); });
	std::transform(primitiveIndices.begin(), primitiveIndices.end(), std::back_inserter(indexCounts), [&](std::vector<Index>& indices) { return static_cast<uint32_t>(indices.size()); });
	std::transform(primitiveVertices.begin(), primitiveVertices.end(), std::back_inserter(vertexBuffers),
				   [&](std::vector<Vertex>& vertices) {
					   return std::make_unique<Buffer>(device, dmm, rth, vk::BufferCreateInfo{}
													   .setSize(static_cast<uint32_t>(vertices.size() * sizeof(vertices.front())))
													   .setUsage(vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress),
													   vk::ArrayProxyNoTemporaries{ static_cast<uint32_t>(vertices.size() * sizeof(Vertex)), (char*)vertices.data() }, MemoryStorage::DevicePersistent);
				   });
	std::transform(primitiveIndices.begin(), primitiveIndices.end(), std::back_inserter(indexBuffers),
				   [&](std::vector<Index>& indices) {
					   return std::make_unique<Buffer>(device, dmm, rth, vk::BufferCreateInfo{}
													   .setSize(indices.size() * sizeof(indices.front()))
													   .setUsage(vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress),
													   vk::ArrayProxyNoTemporaries{ static_cast<uint32_t>(indices.size() * sizeof(Index)), (char*)indices.data() }, MemoryStorage::DevicePersistent);
				   });
}

}