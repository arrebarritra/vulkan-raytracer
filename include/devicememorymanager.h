#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_shared.hpp>
#include <logging.h>
#include <map>
#include <list>

namespace vkrt {

// Based on https://gpuopen.com/learn/vulkan-device-memory/
namespace MemoryStorage {

constexpr vk::MemoryPropertyFlags DevicePersistent = vk::MemoryPropertyFlagBits::eDeviceLocal;
constexpr vk::MemoryPropertyFlags DeviceDynamic = vk::MemoryPropertyFlagBits::eDeviceLocal | vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
constexpr vk::MemoryPropertyFlags HostStaging = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
constexpr vk::MemoryPropertyFlags HostDownload = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostCached;

};

// Vulkan memory management singleton
class DeviceMemoryManager {
	struct Allocation;

public:

	DeviceMemoryManager(vk::Device device, const vk::PhysicalDevice physicalDevice);
	// make non-copyable
	DeviceMemoryManager(const DeviceMemoryManager&) = delete;
	DeviceMemoryManager& operator=(const DeviceMemoryManager&) = delete;

	struct MemoryBlock {
		friend struct Allocation;
	public:
		MemoryBlock(vk::DeviceMemory memory, vk::DeviceSize size, vk::DeviceSize offset);
		~MemoryBlock();

		vk::DeviceMemory memory;
		vk::DeviceSize offset, size;

	private:
		// Store memory blocks in doubly linked list
		MemoryBlock* prev = nullptr;
		MemoryBlock* next = nullptr;

		void append(MemoryBlock* mb);
		void prepend(MemoryBlock* mb);
	};

	std::unique_ptr<MemoryBlock> allocateResource(const vk::MemoryRequirements& memReqs, vk::MemoryPropertyFlags memType);
	class MemoryTypeUnavailableError : std::exception {
		const char* what() const override { return "Could not find requested memory type"; };
	};
private:

	struct Allocation {
		Allocation(vk::UniqueDeviceMemory memory, vk::DeviceSize size);
		std::unique_ptr<MemoryBlock> allocateMemoryBlock(const vk::MemoryRequirements& memReqs, uint32_t memTypeIdx);


		vk::UniqueDeviceMemory memory;
		vk::DeviceSize size;

		// Dummy elements to keep track of head and tail of double linked list
		MemoryBlock head;
		MemoryBlock tail; // Keep track of offset

		class SubAllocationFailedError : std::exception {
			const char* what() const override { return "Failed to sub-allocate memory block"; };
		};
	};

	const vk::PhysicalDevice physicalDevice;
	vk::Device device;

	std::vector<std::vector<std::unique_ptr<Allocation>>> allocations;
	std::vector<vk::DeviceSize> allocBlockSizes;
	static const std::map<vk::MemoryPropertyFlags, vk::DeviceSize> storageBlockSizes;

	void setAllocBlockSizes();
	uint32_t findMemoryTypeIdx(const vk::MemoryRequirements& memReqs, const vk::MemoryPropertyFlags requiredProperties);
	void allocateDeviceMemory(vk::DeviceSize size, uint32_t memTypeIdx);
};

}