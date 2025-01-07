#include <managedresource.h>

namespace vkrt {

ManagedResource::ManagedResource(vk::SharedDevice device, DeviceMemoryManager& dmm, ResourceTransferHandler& rth, vk::MemoryPropertyFlags memProps, bool tranferRead, bool transferWrite)
	: device(device), dmm(dmm), rth(rth), memProps(memProps)
{
	if (tranferRead) {
		vk::Fence readFinishedFenceTmp = device->createFence(vk::FenceCreateInfo{}.setFlags(vk::FenceCreateFlagBits::eSignaled));
		readFinishedFence = vk::SharedHandle(readFinishedFenceTmp, device);
	}
	if (transferWrite) {
		vk::Fence writeFinishedFenceTmp = device->createFence(vk::FenceCreateInfo{}.setFlags(vk::FenceCreateFlagBits::eSignaled));
		writeFinishedFence = vk::SharedHandle(writeFinishedFenceTmp, device);
	}
}

ManagedResource::~ManagedResource() {
	// Complete pending submits
	std::vector<vk::Fence> fences;
	fences.reserve(2);
	if (writeFinishedFence) fences.push_back(*writeFinishedFence);
	if (readFinishedFence) fences.push_back(*readFinishedFence);
	if (fences.size() > 0)
		CHECK_VULKAN_RESULT(device->waitForFences(fences, vk::True, std::numeric_limits<uint64_t>::max()));
}

};
