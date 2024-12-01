#include <devicememorymanager.h>
#include <utils.h>
#include <queue>

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
	auto memoryProperties = physicalDevice.getMemoryProperties();

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
	auto memoryProperties = physicalDevice.getMemoryProperties();

	for (uint32_t memIdx = 0u; memIdx < memoryProperties.memoryTypeCount; memIdx++) {
		auto memType = memoryProperties.memoryTypes[memIdx];
		if (!(1 << memIdx & memReqs.memoryTypeBits)) continue; // memory type bits do not match
		if ((memType.propertyFlags & requiredProperties) == requiredProperties) return memIdx;
	}

	throw MemoryTypeUnavailableError();
}

std::unique_ptr<DeviceMemoryManager::MemoryBlock> DeviceMemoryManager::allocateResource(const vk::MemoryRequirements& memReqs, vk::MemoryPropertyFlags memProps, AllocationStrategy as) {
	auto memTypeIdx = findMemoryTypeIdx(memReqs, memProps);
	if (allocations[memTypeIdx].size() == 0) allocations[memTypeIdx].push_back(std::unique_ptr<Allocation>(new Allocation(*this, memTypeIdx, memProps)));

	switch (as) {
		case AllocationStrategy::Fast:
		case AllocationStrategy::Balanced:
			try {
				return allocations[memTypeIdx].back()->allocateMemoryBlock(memReqs, as);
			} catch (Allocation::SubAllocationFailedError& e) {}
			break;
		case AllocationStrategy::Optimal:
			for (auto& allocation : allocations[memTypeIdx]) {
				try {
					if (allocation->size - allocation->bytesUsed > memReqs.size)
						return allocation->allocateMemoryBlock(memReqs, as);
				} catch (Allocation::SubAllocationFailedError& e) {}
			}
			break;
		case AllocationStrategy::Heuristic:
			std::priority_queue < Allocation*, std::priority_queue<Allocation*>::container_type, decltype(allocHeuristicCmp)> allocPrio;
			for (auto& allocation : allocations[memTypeIdx]) {
				// Try fast strategy
				try {
					return allocation->allocateMemoryBlock(memReqs, AllocationStrategy::Fast);
				} catch (Allocation::SubAllocationFailedError& e) {};
				allocPrio.push(allocation.get());
			}
			// If fast strategy does not work, choose allocation with best heuristic
			for (; !allocPrio.empty(); allocPrio.pop()) {
				try {
					return allocPrio.top()->allocateMemoryBlock(memReqs, as);
				} catch (Allocation::SubAllocationFailedError& e) {}
			}
			break;
	}

	// If we cannot suballocate, create new allocation
	allocations[memTypeIdx].push_back(std::unique_ptr<Allocation>(new Allocation(*this, memTypeIdx, memProps)));
	return allocations[memTypeIdx].back()->allocateMemoryBlock(memReqs, AllocationStrategy::Fast);
}

// TODO account for linear/non-linear resources
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
	, subAllocations(0u), bytesUsed(0u)
{
	auto memoryAllocFI = vk::MemoryAllocateFlagsInfo{}.setFlags(vk::MemoryAllocateFlagBits::eDeviceAddress); // Enable device addresses
	auto memoryAllocInfo = vk::MemoryAllocateInfo{}
		.setPNext(&memoryAllocFI)
		.setAllocationSize(size)
		.setMemoryTypeIndex(memTypeIdx);

	memory = dmm.device->allocateMemoryUnique(memoryAllocInfo);
	if (memProps & vk::MemoryPropertyFlagBits::eHostVisible) mapping = static_cast<char*>(dmm.device->mapMemory(*memory, 0u, vk::WholeSize));
}

DeviceMemoryManager::Allocation::~Allocation() {
	// All suballocations should have been destroyed, but check for sanity
	assert(head == nullptr && tail == nullptr);
	assert(subAllocations == 0);

	if (mapping) {
		if (!(memProps & vk::MemoryPropertyFlagBits::eHostCoherent))
			dmm.device->flushMappedMemoryRanges(vk::MappedMemoryRange{}
												.setMemory(*memory)
												.setOffset(0u)
												.setSize(vk::WholeSize));
		dmm.device->unmapMemory(*memory);
	}
}

std::unique_ptr<DeviceMemoryManager::MemoryBlock> DeviceMemoryManager::Allocation::allocateMemoryBlock(const vk::MemoryRequirements& memReqs, AllocationStrategy as) {
	std::unique_ptr<MemoryBlock> mb;
	vk::DeviceSize blockOffset, blockPadding;
	std::tuple<MemoryBlock*, MemoryBlock*> neighbours(nullptr, nullptr);

	switch (as) {
		case AllocationStrategy::Fast:
			// Fit allocation to required alignment
			blockOffset = utils::alignedOffset(offset, memReqs.alignment);
			blockPadding = utils::paddingSize(memReqs.size, memReqs.alignment);
			if (blockOffset + memReqs.size + blockPadding > size) { throw SubAllocationFailedError(); }
			neighbours = std::make_tuple(tail, nullptr);
			break;
		case AllocationStrategy::Balanced:
		case AllocationStrategy::Optimal:
		case AllocationStrategy::Heuristic:
			for (auto mbIt = head; mbIt != tail; mbIt = mbIt->next) {
				auto baseOffset = mbIt == head ? 0 : mbIt->prev->offset + mbIt->prev->size + mbIt->prev->padding;
				blockOffset = utils::alignedOffset(baseOffset, memReqs.alignment);
				blockPadding = utils::paddingSize(memReqs.size, memReqs.alignment);

				if (blockOffset + memReqs.size + blockPadding < mbIt->offset) {
					neighbours = std::make_tuple(mbIt->prev, mbIt);
					break;
				}
			}
			// Appending to last
			blockOffset = utils::alignedOffset(offset, memReqs.alignment);
			blockPadding = utils::paddingSize(memReqs.size, memReqs.alignment);
			if (blockOffset + memReqs.size + blockPadding > size) { throw SubAllocationFailedError(); }
			neighbours = std::make_tuple(tail, nullptr);
			break;
	}

	mb = std::make_unique<MemoryBlock>(*this, blockOffset, memReqs.size, blockPadding, mapping ? mapping + blockOffset : nullptr);

	assert((head == nullptr && tail == nullptr) || (head != nullptr && tail != nullptr));
	if (neighbours == std::make_tuple<MemoryBlock*, MemoryBlock*>(nullptr, nullptr)) {
		head = mb.get();
		tail = mb.get();
	} else if (std::get<0>(neighbours)) {
		std::get<0>(neighbours)->append(mb.get());
	} else if (std::get<1>(neighbours)) {
		std::get<1>(neighbours)->prepend(mb.get());
	}
	offset = std::max(offset, mb->offset + mb->size + mb->padding);

	// Recalculate heuristics
	subAllocations++;
	bytesUsed += mb->size + mb->padding;

	assert(subAllocations >= 0u);
	assert(bytesUsed >= 0u && bytesUsed <= size);

	return mb;
}

void DeviceMemoryManager::Allocation::syncRemoveMemoryBlock(MemoryBlock* mb)
{
	if (mb == tail) {
		tail = mb->prev;
		offset = tail ? tail->offset + tail->size + tail->padding : 0u;
	}
	if (mb == head)
		head = mb->next;

	// Recalculate heuristics
	subAllocations--;
	bytesUsed -= mb->size + mb->padding;
	// largestUsedBlock cannot be determined without search, we let it be the largest block suballocated in the lifetime of the allocation

	assert(subAllocations >= 0u);
	assert(bytesUsed >= 0u && bytesUsed <= size);
}

}