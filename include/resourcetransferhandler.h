#pragma once

#include <vulkan_headers.h>

#include <devicememorymanager.h>
#include <unordered_map>
#include <optional>
#include <managedresource.h>

namespace vkrt {

class ManagedResource;

struct SyncInfo {
	vk::SharedFence fence;
	std::vector<vk::SharedSemaphore> waitSemaphores, signalSemaphores;
};

class ResourceTransferHandler {

public:
	ResourceTransferHandler(vk::SharedDevice device, std::tuple<uint32_t, vk::Queue> transferQueueIdx);

	const void copy(vk::Buffer srcBuffer, vk::Buffer dstBuffer, vk::BufferCopy bfrCp, SyncInfo si, std::unique_ptr<ManagedResource> stagedResource = nullptr);
	const void copy(vk::Image srcImage, vk::Image dstImage, vk::ImageCopy imgCp, vk::ImageLayout dstLayout, SyncInfo si, std::unique_ptr<ManagedResource> stagedResource = nullptr);
	const void copy(vk::Buffer srcBuffer, vk::Image dstImage, vk::BufferImageCopy bfrImgCp, vk::ImageLayout dstLayout, SyncInfo si, std::unique_ptr<ManagedResource> stagedResource = nullptr);
	const void copy(vk::Image srcImage, vk::Buffer dstBuffer, vk::BufferImageCopy bfrImgCp, SyncInfo si, std::unique_ptr<ManagedResource> stagedResource = nullptr);
	const void freeCompletedTransfers();
	const void flushPendingTransfers(vk::ArrayProxyNoTemporaries<vk::SharedFence> fences);
	const void flushPendingTransfers();

private:
	const void submit(vk::CommandBuffer cmdBuffer, SyncInfo si);

	vk::SharedDevice device;
	std::tuple<uint32_t, vk::Queue> transferQueue;

	vk::UniqueCommandPool commandPool;
	std::unordered_map<vk::Fence, std::tuple<vk::UniqueCommandBuffer, SyncInfo, std::unique_ptr<ManagedResource>>> pendingTransfers;
};

}