#pragma once

#include <vulkan_headers.h>

#include <devicememorymanager.h>
#include <optional>

namespace vkrt {

class ResourceCopyHandler {

public:
	ResourceCopyHandler(vk::SharedDevice device, std::tuple<uint32_t, vk::Queue> transferQueueIdx);

	const vk::SharedFence& submit(vk::ArrayProxyNoTemporaries<vk::SharedSemaphore> waitSemaphores = nullptr, vk::ArrayProxyNoTemporaries<vk::SharedSemaphore> signalSemaphores = nullptr, std::optional<vk::SharedFence> fence = std::nullopt);
	const vk::SharedFence& recordCopyCmd(vk::Buffer srcBuffer, vk::Buffer dstBuffer, vk::BufferCopy bfrCp);
	const vk::SharedFence& recordCopyCmd(vk::Image srcImage, vk::Image dstImageBuffer, vk::ImageCopy imgCp, vk::ImageLayout dstLayout = {});
	const vk::SharedFence& recordCopyCmd(vk::Buffer srcBuffer, vk::Image dstImage, vk::BufferImageCopy bfrImgCp, vk::ImageLayout dstLayout = {});
	const vk::SharedFence& recordCopyCmd(vk::Image srcImage, vk::Buffer dstBuffer, vk::BufferImageCopy bfrImgCp);

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