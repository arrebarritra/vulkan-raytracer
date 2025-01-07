#pragma once

#include <vulkan_headers.h>

#include <devicememorymanager.h>
#include <scene.h>

namespace vkrt {

class AccelerationStructure {

public:
	AccelerationStructure(vk::SharedDevice device, DeviceMemoryManager& dmm, ResourceTransferHandler& rth, Scene& scene, std::tuple<uint32_t, vk::Queue> computeQueue);

	vk::SharedFence buildFinishedFence;

	vk::UniqueAccelerationStructureKHR tlas;
	std::unique_ptr<Buffer> tlasBuffer;

	std::vector<vk::UniqueAccelerationStructureKHR> blas;
	std::vector<std::unique_ptr<Buffer>> blasBuffers;
	std::unique_ptr<Buffer> transformBuffer;

	vk::SharedFence rebuild(vk::ArrayProxyNoTemporaries<vk::SharedSemaphore> waitSemaphores = nullptr, vk::ArrayProxyNoTemporaries<vk::SharedSemaphore> signalSemaphores = nullptr);
	vk::SharedFence update(vk::ArrayProxyNoTemporaries<vk::SharedSemaphore> waitSemaphores = nullptr, vk::ArrayProxyNoTemporaries<vk::SharedSemaphore> signalSemaphores = nullptr);
private:
	vk::SharedDevice device;
	DeviceMemoryManager& dmm;
	ResourceTransferHandler& rth;
	Scene& scene;

	std::tuple<uint32_t, vk::Queue> computeQueue;
	vk::UniqueCommandPool commandPool;
	vk::UniqueCommandBuffer asBuildCmdBuffer;

	vk::SharedFence build(vk::BuildAccelerationStructureModeKHR mode, vk::ArrayProxyNoTemporaries<vk::SharedSemaphore> waitSemaphores = nullptr, vk::ArrayProxyNoTemporaries<vk::SharedSemaphore> signalSemaphores = nullptr);
	void buildBLAS(std::vector<std::unique_ptr<Buffer>>& scratchBuffers, vk::BuildAccelerationStructureModeKHR mode);
	void buildTLAS(std::unique_ptr<Buffer>& instanceBuffer, std::unique_ptr<Buffer>& scratchBuffer, vk::BuildAccelerationStructureModeKHR mode);
};

}