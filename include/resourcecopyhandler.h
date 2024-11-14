#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_shared.hpp>
#include <devicememorymanager.h>

namespace vkrt {

class ResourceCopyHandler {

public:
	ResourceCopyHandler(vk::SharedDevice device, std::tuple<uint32_t, vk::Queue> transferQueueIdx);

	vk::SharedFence submit(vk::ArrayProxyNoTemporaries<vk::SharedSemaphore> waitSemaphores = nullptr, vk::ArrayProxyNoTemporaries<vk::SharedSemaphore> signalSemaphores = nullptr, std::optional<vk::SharedFence> fence = std::nullopt);
	vk::SharedFence recordCopyCmd(vk::Buffer srcBuffer, vk::Buffer dstBuffer, vk::BufferCopy bfrCp);
	vk::SharedFence recordCopyCmd(vk::Image srcImage, vk::Image dstImageBuffer, vk::ImageCopy imgCp, vk::ImageLayout dstLayout = {});
	vk::SharedFence recordCopyCmd(vk::Buffer srcBuffer, vk::Image dstImage, vk::BufferImageCopy bfrImgCp, vk::ImageLayout dstLayout = {});
	vk::SharedFence recordCopyCmd(vk::Image srcImage, vk::Buffer dstBuffer, vk::BufferImageCopy bfrImgCp);

private:
	void startRecording();

	vk::SharedDevice device;
	std::tuple<uint32_t, vk::Queue> transferQueue;

	bool recording = false;
	static const uint32_t POOL_SIZE = 4;
	uint32_t idx = 0;
	vk::UniqueCommandPool commandPool;
	std::array<vk::UniqueCommandBuffer, POOL_SIZE> transferCmdBuffers;
	std::array<vk::SharedFence, POOL_SIZE> transferFences;
};

}