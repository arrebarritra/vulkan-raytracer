#include <mesh.h>

namespace vkrt {

Mesh::Mesh(vk::SharedDevice device, DeviceMemoryManager& dmm, ResourceCopyHandler& rch, vk::ArrayProxyNoTemporaries<glm::vec3> vertices, vk::ArrayProxyNoTemporaries<uint32_t> indices)
	: transparent(false), nVertices(vertices.size()), nIndices(indices.size())
	, vertices(std::make_unique<Buffer>(device, dmm, rch, vk::BufferCreateInfo{}
			   .setSize(static_cast<uint32_t>(vertices.size() * sizeof(vertices.front())))
			   .setUsage(vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress),
			   vk::ArrayProxyNoTemporaries{ static_cast<uint32_t>(vertices.size() * sizeof(vertices.front())), (char*)vertices.data() }, MemoryStorage::DevicePersistent))
	, indices(std::make_unique<Buffer>(device, dmm, rch, vk::BufferCreateInfo{}
			  .setSize(indices.size() * sizeof(indices.front()))
			  .setUsage(vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress),
			  vk::ArrayProxyNoTemporaries{ static_cast<uint32_t>(indices.size() * sizeof(indices.front())), (char*)indices.data() }, MemoryStorage::DevicePersistent))
{}

}