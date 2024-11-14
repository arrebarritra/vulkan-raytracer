#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_shared.hpp>

#include <logging.h>
#include <devicememorymanager.h>
#include <resourcecopyhandler.h>

#include <optional>

namespace vkrt {

class ManagedResource {

public:
	// query status of issued read/write
	vk::SharedFence readFinishedFence, writeFinishedFence;

protected:
	ManagedResource(vk::SharedDevice device, DeviceMemoryManager& dmm, ResourceCopyHandler& rch, vk::MemoryPropertyFlags memProps);
	// Disable copy (we don't want to inadvertently destroy allocated memory blocks)
	ManagedResource(const ManagedResource&) = delete;
	ManagedResource operator=(const ManagedResource&) = delete;

	virtual std::optional<vk::SharedFence> write(vk::ArrayProxyNoTemporaries<char> data) = 0;
	virtual std::vector<char> read() = 0;

	vk::SharedDevice device;
	DeviceMemoryManager& dmm;
	ResourceCopyHandler& rch;

	std::unique_ptr<DeviceMemoryManager::MemoryBlock> memBlock;
	vk::MemoryPropertyFlags memProps;
};

}