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

class Image;
class Buffer : public ManagedResource {

public:
	Buffer(vk::SharedDevice device, DeviceMemoryManager& dmm, ResourceCopyHandler& rch, vk::BufferCreateInfo bufferCI, vk::ArrayProxyNoTemporaries<char> data = nullptr,
		   const vk::MemoryPropertyFlags& memProps = BufferMemoryUsage::Auto, DeviceMemoryManager::AllocationStrategy as = DeviceMemoryManager::AllocationStrategy::Fast);

	vk::Buffer operator*() { return *buffer; }

	std::optional<vk::SharedFence> write(vk::ArrayProxyNoTemporaries<char> data, vk::DeviceSize offset = 0Ui64);
	std::vector<char> read();

	vk::SharedFence copyFrom(vk::Buffer srcBuffer, vk::BufferCopy bfrCp);
	vk::SharedFence copyFrom(Buffer& srcBuffer, vk::BufferCopy bfrCp);
	vk::SharedFence copyFrom(vk::Image srcImage, vk::BufferImageCopy bfrImgCp);
	vk::SharedFence copyFrom(Image& srcImage, vk::BufferImageCopy bfrImgCp);
	vk::SharedFence copyTo(vk::Buffer dstBuffer, vk::BufferCopy bfrCp);
	vk::SharedFence copyTo(Buffer& dstBuffer, vk::BufferCopy bfrCp);
	vk::SharedFence copyTo(vk::Image dstImage, vk::BufferImageCopy bfrImgCp, vk::ImageLayout dstLayout = {});
	vk::SharedFence copyTo(Image& dstImage, vk::BufferImageCopy bfrImgCp, vk::ImageLayout dstLayout = {});

	vk::BufferCreateInfo bufferCI;
	vk::UniqueBuffer buffer;
};

}