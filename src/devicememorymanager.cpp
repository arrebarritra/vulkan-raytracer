#include <devicememorymanager.h>

namespace vkrt {

const std::map<vk::MemoryPropertyFlags, vk::DeviceSize> DeviceMemoryManager::storageBlockSizes = {
	{ MemoryStorage::DevicePersistent, 256u * (1u << 20u) },
	{ MemoryStorage::DevicePersistent, 64u * (1u << 20u) }
};

DeviceMemoryManager::DeviceMemoryManager(vk::Device device, vk::PhysicalDevice physicalDevice)
	: physicalDevice(physicalDevice)
	, device(device)
	, allocations(std::vector<std::vector<std::unique_ptr<Allocation>>>(physicalDevice.getMemoryProperties().memoryTypeCount))
{
	allocBlockSizes = std::vector<vk::DeviceSize>(physicalDevice.getMemoryProperties().memoryTypeCount);
	setAllocBlockSizes();
}

void DeviceMemoryManager::setAllocBlockSizes() {
	auto& memoryProperties = physicalDevice.getMemoryProperties();

	for (uint32_t memIdx = 0u; memIdx < memoryProperties.memoryTypeCount; memIdx++) {
		auto memPropFlags = memoryProperties.memoryTypes[memIdx].propertyFlags;

		vk::DeviceSize maxBlockSize = memoryProperties.memoryHeaps[memoryProperties.memoryTypes[memIdx].heapIndex].size / 4u;
		vk::DeviceSize blockSize = 256u * (1u << 20u);
		try {
			storageBlockSizes.at(memPropFlags);
		} catch (...) {}

		allocBlockSizes[memIdx] = std::min(blockSize, maxBlockSize);
	}
}

// Based on https://registry.khronos.org/vulkan/specs/1.0/html/vkspec.html#VkMemoryType example
uint32_t DeviceMemoryManager::findMemoryTypeIdx(const vk::MemoryRequirements& memReqs, const vk::MemoryPropertyFlags requiredProperties) {
	auto& memoryProperties = physicalDevice.getMemoryProperties();

	for (uint32_t memIdx = 0u; memIdx < memoryProperties.memoryTypeCount; memIdx++) {
		auto& memType = memoryProperties.memoryTypes[memIdx];
		if (!(1 << memIdx & memReqs.memoryTypeBits)) continue; // memory type bits do not match
		if ((memType.propertyFlags & requiredProperties) == requiredProperties) return memIdx;
	}

	throw MemoryTypeUnavailableError();
}

void DeviceMemoryManager::allocateDeviceMemory(vk::DeviceSize size, uint32_t memTypeIdx) {
	auto& memoryAllocInfo = vk::MemoryAllocateInfo{}
		.setAllocationSize(size)
		.setMemoryTypeIndex(memTypeIdx);
	auto& alloc = std::make_unique<Allocation>(device.allocateMemoryUnique(memoryAllocInfo), size);
	allocations[memTypeIdx].push_back(std::move(alloc));
}

std::unique_ptr<DeviceMemoryManager::MemoryBlock> vkrt::DeviceMemoryManager::allocateResource(const vk::MemoryRequirements& memReqs, vk::MemoryPropertyFlags memProps) {
	auto memTypeIdx = findMemoryTypeIdx(memReqs, memProps);
	if (allocations[memTypeIdx].size() == 0) allocateDeviceMemory(allocBlockSizes[memTypeIdx], memTypeIdx);

	try {
		return allocations[memTypeIdx].back()->allocateMemoryBlock(memReqs, memTypeIdx);
	} catch (Allocation::SubAllocationFailedError& e) {
		allocateDeviceMemory(allocBlockSizes[memTypeIdx], memTypeIdx);
		return allocations[memTypeIdx].back()->allocateMemoryBlock(memReqs, memTypeIdx);
	}
}

DeviceMemoryManager::MemoryBlock::MemoryBlock(vk::DeviceMemory memory, vk::DeviceSize size, vk::DeviceSize offset)
	: memory(memory)
	, size(size)
	, offset(offset)
{}

DeviceMemoryManager::MemoryBlock::~MemoryBlock() {
	if (next) {
		if (next->size == 0u) next->offset = offset; // bring back offset pointer if removing last element
		next->prev = prev;
	}
	if (prev) prev->next = next;
}

void DeviceMemoryManager::MemoryBlock::append(MemoryBlock* mb) {
	next->prev = mb;
	next = mb;
}

void DeviceMemoryManager::MemoryBlock::prepend(MemoryBlock* mb) {
	if (size == 0u) offset += mb->size; // move offset if we are prepending to tail
	prev->next = mb;
	prev = mb;
}

DeviceMemoryManager::Allocation::Allocation(vk::UniqueDeviceMemory memory, vk::DeviceSize size)
	: memory(std::move(memory))
	, size(size)
	, head(memory.get(), 0u, 0u)
	, tail(memory.get(), 0u, 0u)
{
	head.next = &tail;
	tail.prev = &head;
}

std::unique_ptr<DeviceMemoryManager::MemoryBlock> DeviceMemoryManager::Allocation::allocateMemoryBlock(const vk::MemoryRequirements& memReqs, uint32_t memTypeIdx) {
	// Fit allocation to required alignment
	auto offset = tail.offset % memReqs.alignment ? \
		tail.offset + memReqs.alignment - (tail.offset % memReqs.alignment) : tail.offset;
	auto blockSize = memReqs.size % memReqs.alignment ? \
		memReqs.size + memReqs.alignment - (memReqs.size % memReqs.alignment) : memReqs.size;
	if (tail.offset + blockSize > size) { throw SubAllocationFailedError(); }

	auto mb = std::make_unique<MemoryBlock>(memory.get(), blockSize, tail.offset);
	tail.prepend(mb.get());

	return mb;
}

}