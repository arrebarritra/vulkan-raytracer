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

class ManagedResource;
class Buffer;
class ResourceTransferHandler;
struct SyncInfo;
class Image : public ManagedResource {
public:
	Image(vk::SharedDevice device, DeviceMemoryManager& dmm, ResourceTransferHandler& rth, vk::ImageCreateInfo imageCI, vk::ArrayProxyNoTemporaries<char> data = nullptr, vk::ImageLayout targetLayout = vk::ImageLayout::eUndefined,
		  const vk::MemoryPropertyFlags& memProps = ImageMemoryUsage::Auto, DeviceMemoryManager::AllocationStrategy as = DeviceMemoryManager::AllocationStrategy::Heuristic);
	Image(vk::SharedDevice device, DeviceMemoryManager& dmm, ResourceTransferHandler& rth, vk::ImageCreateInfo imageCI, std::filesystem::path imageFile, vk::ImageLayout targetLayout = vk::ImageLayout::eUndefined,
		  const vk::MemoryPropertyFlags& memProps = ImageMemoryUsage::Auto, DeviceMemoryManager::AllocationStrategy as = DeviceMemoryManager::AllocationStrategy::Heuristic);

	vk::Image operator*() { return *image; }

	std::optional<vk::SharedFence> write(vk::ArrayProxyNoTemporaries<char> data, vk::ImageLayout targetLayout = vk::ImageLayout::eUndefined);
	std::vector<char> read();

	void copyFrom(vk::Image srcImage, vk::ImageCopy imgCp, vk::ImageLayout layout, SyncInfo si, std::unique_ptr<ManagedResource> stagedResource = nullptr);
	void copyFrom(Image& srcImage, vk::ImageCopy imgCp, vk::ImageLayout layout, SyncInfo si, std::unique_ptr<ManagedResource> stagedResource = nullptr);
	void copyFrom(vk::Buffer srcBuffer, vk::BufferImageCopy bfrImgCp, vk::ImageLayout layout, SyncInfo si, std::unique_ptr<ManagedResource> stagedResource = nullptr);
	void copyFrom(Buffer& srcBuffer, vk::BufferImageCopy bfrImgCp, vk::ImageLayout layout, SyncInfo si, std::unique_ptr<ManagedResource> stagedResource = nullptr);
	void copyTo(vk::Image dstImage, vk::ImageCopy imgCp, vk::ImageLayout dstLayout, SyncInfo si, std::unique_ptr<ManagedResource> stagedResource = nullptr);
	void copyTo(Image& dstImage, vk::ImageCopy imgCp, vk::ImageLayout dstLayout, SyncInfo si, std::unique_ptr<ManagedResource> stagedResource = nullptr);
	void copyTo(vk::Buffer dstBuffer, vk::BufferImageCopy bfrImgCp, SyncInfo si, std::unique_ptr<ManagedResource> stagedResource = nullptr);
	void copyTo(Buffer& dstBuffer, vk::BufferImageCopy bfrImgCp, SyncInfo si, std::unique_ptr<ManagedResource> stagedResource = nullptr);

	vk::ImageCreateInfo imageCI;
	vk::UniqueImage image;
};

}