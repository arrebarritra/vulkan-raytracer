#pragma once

#include <vulkan_headers.h>

#include <managedresource.h>
#include <buffer.h>
#include <filesystem>

#include <stb_image.h>

namespace vkrt {

namespace ImageMemoryUsage {

constexpr vk::MemoryPropertyFlags Auto = {}; // TODO: implement automatic flag selection based on ImageUsageFlags
constexpr vk::MemoryPropertyFlags Framebuffer = MemoryStorage::DevicePersistent;
constexpr vk::MemoryPropertyFlags RenderTarget = MemoryStorage::DevicePersistent;
constexpr vk::MemoryPropertyFlags Texture = MemoryStorage::DevicePersistent;

}

class Buffer;
class Image : public ManagedResource {
public:
	Image(vk::SharedDevice device, DeviceMemoryManager& dmm, ResourceCopyHandler& rch, vk::ImageCreateInfo imageCI, vk::ArrayProxyNoTemporaries<char> data = nullptr, vk::ImageLayout targetLayout = vk::ImageLayout::eUndefined,
		  const vk::MemoryPropertyFlags& memProps = ImageMemoryUsage::Auto, DeviceMemoryManager::AllocationStrategy as = DeviceMemoryManager::AllocationStrategy::Heuristic);
	Image(vk::SharedDevice device, DeviceMemoryManager& dmm, ResourceCopyHandler& rch, vk::ImageCreateInfo imageCI, std::filesystem::path imageFile, vk::ImageLayout targetLayout = vk::ImageLayout::eUndefined,
		  const vk::MemoryPropertyFlags& memProps = ImageMemoryUsage::Auto, DeviceMemoryManager::AllocationStrategy as = DeviceMemoryManager::AllocationStrategy::Heuristic);

	vk::Image operator*() { return *image; }

	std::optional<vk::SharedFence> write(vk::ArrayProxyNoTemporaries<char> data, vk::ImageLayout targetLayout = vk::ImageLayout::eUndefined);
	std::vector<char> read();

	vk::SharedFence copyFrom(vk::Image srcImage, vk::ImageCopy imgCp, vk::ImageLayout layout = {});
	vk::SharedFence copyFrom(Image& srcImage, vk::ImageCopy imgCp, vk::ImageLayout layout = {});
	vk::SharedFence copyFrom(vk::Buffer srcBuffer, vk::BufferImageCopy bfrImgCp, vk::ImageLayout layout = {});
	vk::SharedFence copyFrom(Buffer& srcBuffer, vk::BufferImageCopy bfrImgCp, vk::ImageLayout layout = {});
	vk::SharedFence copyTo(vk::Image dstImage, vk::ImageCopy imgCp, vk::ImageLayout dstLayout = {});
	vk::SharedFence copyTo(Image& dstImage, vk::ImageCopy imgCp, vk::ImageLayout dstLayout = {});
	vk::SharedFence copyTo(vk::Buffer dstBuffer, vk::BufferImageCopy bfrImgCp);
	vk::SharedFence copyTo(Buffer& dstBuffer, vk::BufferImageCopy bfrImgCp);

	vk::ImageCreateInfo imageCI;
	vk::UniqueImage image;
};

}