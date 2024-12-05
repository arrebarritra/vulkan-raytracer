#include <managedresource.h>

vkrt::ManagedResource::ManagedResource(vk::SharedDevice device, DeviceMemoryManager& dmm, ResourceCopyHandler& rch, vk::MemoryPropertyFlags memProps, bool tranferRead, bool transferWrite)
	: device(device), dmm(dmm), rch(rch), memProps(memProps)
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

vkrt::ManagedResource::~ManagedResource() {
	// Complete pending submits

	if ((*readFinishedFence != nullptr && device->getFenceStatus(*readFinishedFence) == vk::Result::eNotReady) ||
		(*writeFinishedFence !=nullptr && device->getFenceStatus(*writeFinishedFence) == vk::Result::eNotReady))
		device->waitForFences(*rch.submit(), vk::True, std::numeric_limits<uint64_t>::max());
}
