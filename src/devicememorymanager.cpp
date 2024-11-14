#include <devicememorymanager.h>

namespace vkrt {

const std::map<vk::MemoryPropertyFlags, vk::DeviceSize> DeviceMemoryManager::storageBlockSizes = {
	{ MemoryStorage::DevicePersistent, 256u * (1u << 20u) },
	{ MemoryStorage::DevicePersistent, 64u * (1u << 20u) },
	{ MemoryStorage::HostStaging, 256u * (1u << 20u) },
	{ MemoryStorage::HostDownload, 256u * (1u << 20u) }
};

DeviceMemoryManager::DeviceMemoryManager(vk::SharedDevice device, vk::PhysicalDevice physicalDevice)
	: device(device)
	, physicalDevice(physicalDevice)
	, allocations(std::vector<std::vector<std::unique_ptr<Allocation>>>(physicalDevice.getMemoryProperties().memoryTypeCount))
{
	allocBlockSizes = std::vector<vk::DeviceSize>(physicalDevice.getMemoryProperties().memoryTypeCount);
	setAllocBlockSizes();
}

void DeviceMemoryManager::setAllocBlockSizes() {
	auto& memoryProperties = physicalDevice.getMemoryProperties();

	for (uint32_t memIdx = 0u; memIdx < memoryProperties.memoryTypeCount; memIdx++) {
		auto memPropFlags = memoryProperties.memoryTypes[memIdx].propertyFlags;
		auto memoryBytes = memoryProperties.memoryHeaps[memoryProperties.memoryTypes[memIdx].heapIndex].size;
		vk::DeviceSize maxBlockSize = memoryBytes > (1 << 30) ? 256u * (1u << 20u) : memoryBytes / 8u;
		vk::DeviceSize blockSize = 256u * (1u << 20u);
		try {
			blockSize = storageBlockSizes.at(memPropFlags);
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

std::unique_ptr<DeviceMemoryManager::MemoryBlock> DeviceMemoryManager::allocateResource(const vk::MemoryRequirements& memReqs, vk::MemoryPropertyFlags memProps) {
	auto memTypeIdx = findMemoryTypeIdx(memReqs, memProps);
	if (allocations[memTypeIdx].size() == 0) allocations[memTypeIdx].push_back(std::unique_ptr<Allocation>(new Allocation(*this, memTypeIdx, memProps)));  // TODO: go back to make_unique if friending can work

	try {
		return allocations[memTypeIdx].back()->allocateMemoryBlock(memReqs);
	} catch (Allocation::SubAllocationFailedError& e) {
		allocations[memTypeIdx].push_back(std::unique_ptr<Allocation>(new Allocation(*this, memTypeIdx, memProps)));
		return allocations[memTypeIdx].back()->allocateMemoryBlock(memReqs);
	}
}

// TODO account for linear/non-linear resourcesk
DeviceMemoryManager::MemoryBlock::MemoryBlock(Allocation& allocation, vk::DeviceSize offset, vk::DeviceSize size, vk::DeviceSize padding, char* mapping)
	: allocation(allocation), offset(offset), size(size), padding(padding), mapping(mapping) {}

DeviceMemoryManager::MemoryBlock::~MemoryBlock() {
	if (next) next->prev = prev;
	if (prev) prev->next = next;

	allocation.syncRemoveMemoryBlock(this);
}

void DeviceMemoryManager::MemoryBlock::append(MemoryBlock* mb) {
	if (next) {
		mb->next = next;
		next->prev = mb;
	} else {
		allocation.tail = mb;
	}
	mb->prev = this;
	next = mb;
}

void DeviceMemoryManager::MemoryBlock::prepend(MemoryBlock* mb) {
	if (prev) {
		mb->prev = prev;
		prev->next = mb;
	} else {
		allocation.head = mb;
	}
	mb->next = this;
	prev = mb;
}

DeviceMemoryManager::Allocation::Allocation(DeviceMemoryManager& dmm, uint32_t memTypeIdx, vk::MemoryPropertyFlags memProps)
	: dmm(dmm), memTypeIdx(memTypeIdx), memProps(memProps), size(dmm.allocBlockSizes[memTypeIdx]), offset(0u)
{
	auto& memoryAllocInfo = vk::MemoryAllocateInfo{}
		.setAllocationSize(size)
		.setMemoryTypeIndex(memTypeIdx);
	memory = dmm.device->allocateMemoryUnique(memoryAllocInfo);
	if (memProps & vk::MemoryPropertyFlagBits::eHostVisible) mapping = static_cast<char*>(dmm.device->mapMemory(memory.get(), 0u, size));
}

DeviceMemoryManager::Allocation::~Allocation() {
	// All suballocations should have been destroyed, but check for sanity
	assert(head == nullptr && tail == nullptr);

	if (mapping) {
		if (!(memProps & vk::MemoryPropertyFlagBits::eHostCoherent))
			dmm.device->flushMappedMemoryRanges(vk::MappedMemoryRange{}
												.setMemory(*memory)
												.setOffset(0u)
												.setSize(vk::WholeSize));
		dmm.device->unmapMemory(memory.get());
	}
}

std::unique_ptr<DeviceMemoryManager::MemoryBlock> DeviceMemoryManager::Allocation::allocateMemoryBlock(const vk::MemoryRequirements& memReqs) {
	// Fit allocation to required alignment
	auto blockOffset = offset % memReqs.alignment ? \
		offset + memReqs.alignment - (offset % memReqs.alignment) : offset;
	auto blockPadding = (memReqs.alignment - memReqs.size % memReqs.alignment) % memReqs.alignment;
	if (blockOffset + memReqs.size + blockPadding > size) { throw SubAllocationFailedError(); } // TODO: expand allocation strategies

	auto mb = std::make_unique<MemoryBlock>(*this, blockOffset, memReqs.size, blockPadding, mapping);

	assert((head == nullptr && tail == nullptr) || (head != nullptr && tail != nullptr));
	if (head == nullptr) {
		head = mb.get();
		tail = mb.get();
	} else {
		tail->prepend(mb.get());
		offset = mb->offset + mb->size + mb->padding;
	}

	return mb;
}

void DeviceMemoryManager::Allocation::syncRemoveMemoryBlock(MemoryBlock* mb)
{
	if (mb == tail) {
		tail = mb->prev;
		offset = tail ? tail->offset + tail->size + tail->padding : 0;
	}
	if (mb == head)
		head = mb->next;
}

}