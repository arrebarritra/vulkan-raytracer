#pragma once

#include <vulkan_headers.h>

#include <logging.h>

#include <map>
#include <tuple>
#include <optional>
#include <functional>

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

public:
	struct Allocation;

	DeviceMemoryManager(vk::SharedDevice device, vk::PhysicalDevice physicalDevice);
	// make non-copyable
	DeviceMemoryManager(const DeviceMemoryManager&) = delete;
	DeviceMemoryManager& operator=(const DeviceMemoryManager&) = delete;

	struct MemoryBlock {
		friend struct Allocation;
	public:
		MemoryBlock(Allocation& allocation, vk::DeviceSize offset, vk::DeviceSize size, vk::DeviceSize padding, char* mapping);
		// make non-copyable to prevent inadvertent destructor when passing around, should be wrapped in smart pointer
		MemoryBlock(const MemoryBlock&) = delete;
		MemoryBlock& operator=(const MemoryBlock&) = delete;
		~MemoryBlock();

		Allocation& allocation;
		vk::DeviceSize offset, size, padding;
		char* mapping = nullptr;

	private:
		// Store memory blocks in doubly linked list
		MemoryBlock* prev = nullptr;
		MemoryBlock* next = nullptr;

		void append(MemoryBlock* mb);
		void prepend(MemoryBlock* mb);
	};

	enum class AllocationStrategy {
		Fast, Optimal, Balanced, Heuristic
	};

	struct Allocation {
		friend class DeviceMemoryManager;
		friend struct MemoryBlock;
		// Friend classes for access to constructor/destructor
		friend struct std::default_delete<Allocation>;
	public:
		uint32_t memTypeIdx;
		vk::MemoryPropertyFlags memProps;
		vk::UniqueDeviceMemory memory;
		char* mapping = nullptr;
		const vk::DeviceSize size;
		vk::DeviceSize offset;

	private:
		Allocation(DeviceMemoryManager& dmm, uint32_t memTypeIdx, vk::MemoryPropertyFlags memProps);
		~Allocation();

		std::unique_ptr<MemoryBlock> allocateMemoryBlock(const vk::MemoryRequirements& memReqs, AllocationStrategy as);
		void syncRemoveMemoryBlock(MemoryBlock* mb);

		class SubAllocationFailedError : std::exception {
			const char* what() const override { return "Failed to sub-allocate memory block"; };
		};

		DeviceMemoryManager& dmm;
		MemoryBlock* head = nullptr;
		MemoryBlock* tail = nullptr; // Keep track of head and tail of doubly linked list

		// Heuristics
		uint32_t subAllocations;
		vk::DeviceSize bytesUsed;
	};

	std::unique_ptr<MemoryBlock> allocateResource(const vk::MemoryRequirements& memReqs, vk::MemoryPropertyFlags memProps, AllocationStrategy as = AllocationStrategy::Balanced);
	class MemoryTypeUnavailableError : std::exception {
		const char* what() const override { return "Could not find requested memory type"; };
	};

private:
	const vk::PhysicalDevice physicalDevice;
	vk::SharedDevice device;

	std::vector<std::vector<std::unique_ptr<Allocation>>> allocations;
	std::vector<vk::DeviceSize> allocBlockSizes;
	static const std::map<vk::MemoryPropertyFlags, vk::DeviceSize> storageBlockSizes;

	struct allocHeuristicCmp {
		const std::function<float(const Allocation*)> allocHeuristic = [](const Allocation* a) {
			return (a->size - a->bytesUsed) / a->size + (1.0f / a->subAllocations);
		};
		const float operator()(const Allocation* a, const Allocation* b) const { return allocHeuristic(a) < allocHeuristic(b); }
	};

	void setAllocBlockSizes();

	uint32_t findMemoryTypeIdx(const vk::MemoryRequirements& memReqs, const vk::MemoryPropertyFlags requiredProperties);
};

}