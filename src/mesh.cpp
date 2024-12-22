#include <mesh.h>

namespace vkrt {

Mesh::Mesh(vk::SharedDevice device, DeviceMemoryManager& dmm, ResourceTransferHandler& rth, vk::ArrayProxyNoTemporaries<Vertex> vertices, vk::ArrayProxyNoTemporaries<Index> indices, uint32_t materialIdx)
	: nVertices(vertices.size()), nIndices(indices.size())
	, vertices(std::make_unique<Buffer>(device, dmm, rth, vk::BufferCreateInfo{}
			   .setSize(static_cast<uint32_t>(vertices.size() * sizeof(vertices.front())))
			   .setUsage(vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress),
			   vk::ArrayProxyNoTemporaries{ static_cast<uint32_t>(vertices.size() * sizeof(Vertex)), (char*)vertices.data() }, MemoryStorage::DevicePersistent))
	, indices(std::make_unique<Buffer>(device, dmm, rth, vk::BufferCreateInfo{}
			  .setSize(indices.size() * sizeof(indices.front()))
			  .setUsage(vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress),
			  vk::ArrayProxyNoTemporaries{ static_cast<uint32_t>(indices.size() * sizeof(Index)), (char*)indices.data() }, MemoryStorage::DevicePersistent))
	, materialIdx(materialIdx)
{}

}