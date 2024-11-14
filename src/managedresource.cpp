#include <managedresource.h>

vkrt::ManagedResource::ManagedResource(vk::SharedDevice device, DeviceMemoryManager& dmm, ResourceCopyHandler& rch, vk::MemoryPropertyFlags memProps)
	: device(device), dmm(dmm), rch(rch), memProps(memProps)
{
	vk::Fence readFinishedFenceTmp = device->createFence(vk::FenceCreateInfo{}.setFlags(vk::FenceCreateFlagBits::eSignaled));
	vk::Fence writeFinishedFenceTmp = device->createFence(vk::FenceCreateInfo{}.setFlags(vk::FenceCreateFlagBits::eSignaled));
	readFinishedFence = vk::SharedHandle(readFinishedFenceTmp, device);
	writeFinishedFence = vk::SharedHandle(writeFinishedFenceTmp, device);
}
