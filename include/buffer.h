#pragma once

#include <vulkan_headers.h>

#include <managedresource.h>
#include <image.h>

namespace vkrt {

namespace BufferMemoryUsage {

constexpr vk::MemoryPropertyFlags Auto = {}; // TODO: implement automatic flag selection based on ImageUsageFlags
constexpr vk::MemoryPropertyFlags Buffer = MemoryStorage::DevicePersistent;
constexpr vk::MemoryPropertyFlags UniformBuffer = MemoryStorage::DeviceDynamic;

}

class ManagedResource;
class Image;
class ResourceTransferHandler;
struct SyncInfo;
class Buffer : public ManagedResource {

public:
	Buffer(vk::SharedDevice device, DeviceMemoryManager& dmm, ResourceTransferHandler& rth, vk::BufferCreateInfo bufferCI, vk::ArrayProxyNoTemporaries<char> data = nullptr,
		   const vk::MemoryPropertyFlags& memProps = BufferMemoryUsage::Auto, DeviceMemoryManager::AllocationStrategy as = DeviceMemoryManager::AllocationStrategy::Fast);

	vk::Buffer operator*() { return *buffer; }

	std::optional<vk::SharedFence> write(vk::ArrayProxyNoTemporaries<char> data, vk::DeviceSize offset = 0Ui64);
	std::vector<char> read();

	void copyFrom(vk::Buffer srcBuffer, vk::BufferCopy bfrCp, SyncInfo si, std::unique_ptr<ManagedResource> stagedResource = nullptr);
	void copyFrom(Buffer& srcBuffer, vk::BufferCopy bfrCp, SyncInfo si, std::unique_ptr<ManagedResource> stagedResource = nullptr);
	void copyFrom(vk::Image srcImage, vk::BufferImageCopy bfrImgCp, SyncInfo si, std::unique_ptr<ManagedResource> stagedResource = nullptr);
	void copyFrom(Image& srcImage, vk::BufferImageCopy bfrImgCp, SyncInfo si, std::unique_ptr<ManagedResource> stagedResource = nullptr);
	void copyTo(vk::Buffer dstBuffer, vk::BufferCopy bfrCp, SyncInfo si, std::unique_ptr<ManagedResource> stagedResource = nullptr);
	void copyTo(Buffer& dstBuffer, vk::BufferCopy bfrCp, SyncInfo si, std::unique_ptr<ManagedResource> stagedResource = nullptr);
	void copyTo(vk::Image dstImage, vk::BufferImageCopy bfrImgCp, vk::ImageLayout dstLayout, SyncInfo si, std::unique_ptr<ManagedResource> stagedResource = nullptr);
	void copyTo(Image& dstImage, vk::BufferImageCopy bfrImgCp, vk::ImageLayout dstLayout, SyncInfo si, std::unique_ptr<ManagedResource> stagedResource = nullptr);

	vk::BufferCreateInfo bufferCI;
	vk::UniqueBuffer buffer;
};

}