#pragma once

#include <vulkan_headers.h>

#include <logging.h>
#include <devicememorymanager.h>
#include <resourcetransferhandler.h>

#include <optional>

namespace vkrt {

class ResourceTransferHandler;
class ManagedResource {

public:
	// query status of issued read/write
	vk::SharedFence readFinishedFence, writeFinishedFence;
	virtual ~ManagedResource();

protected:
	ManagedResource(vk::SharedDevice device, DeviceMemoryManager& dmm, ResourceTransferHandler& rth, vk::MemoryPropertyFlags memProps, bool tranferRead = false, bool transferWrite = false);
	// Disable copy (we don't want to inadvertently destroy allocated memory blocks)
	ManagedResource(const ManagedResource&) = delete;
	ManagedResource operator=(const ManagedResource&) = delete;

	vk::SharedDevice device;
	DeviceMemoryManager& dmm;
	ResourceTransferHandler& rth;

	std::unique_ptr<DeviceMemoryManager::MemoryBlock> memBlock;
	vk::MemoryPropertyFlags memProps;
};

}