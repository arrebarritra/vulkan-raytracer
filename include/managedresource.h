#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_shared.hpp>
#include <devicememorymanager.h>

namespace vkrt {

namespace MemoryUsage {

constexpr vk::MemoryPropertyFlags Buffer = MemoryStorage::DevicePersistent;
constexpr vk::MemoryPropertyFlags Framebuffer = MemoryStorage::DevicePersistent;
constexpr vk::MemoryPropertyFlags RenderTarget = MemoryStorage::DevicePersistent;
constexpr vk::MemoryPropertyFlags Texture = MemoryStorage::DevicePersistent;
constexpr vk::MemoryPropertyFlags UniformBuffer = MemoryStorage::DeviceDynamic;
constexpr vk::MemoryPropertyFlags Download = MemoryStorage::HostDownload;

}

template<typename Type, typename CreateInfo, typename Dispatch,
	vk::UniqueHandle<Type, Dispatch>(vk::Device::* createResourceUnique)(
		const CreateInfo&, vk::Optional<const vk::AllocationCallbacks>, const Dispatch&) const,
	void(vk::Device::* bindResourceMemory)(
		Type, vk::DeviceMemory, vk::DeviceSize, const Dispatch&) const>
class ManagedResource {

public:
	ManagedResource(vk::UniqueHandle<Type, Dispatch> resource, vk::ArrayProxy<char> data, vk::Device device, DeviceMemoryManager* dmm);
	ManagedResource(Type resource, vk::ArrayProxy<char> data, vk::Device device, DeviceMemoryManager* dmm)
		: ManagedResource(vk::UniqueHandle<Type, Dispatch>(resource), data device, dmm) {}
	ManagedResource(const CreateInfo& resourceCreateInfo, vk::ArrayProxy<char> data, vk::Device device, DeviceMemoryManager* dmm);

	operator Type() { return resource.get(); }
	void uploadData(vk::ArrayProxy<char> data, vk::MemoryRequirements& memReqs);

private:
	vk::UniqueHandle<Type, vk::DispatchLoaderStatic> resource;
	std::unique_ptr<DeviceMemoryManager::MemoryBlock> memBlock;

	vk::Device device;
	DeviceMemoryManager* dmm;
};

template<typename Type, typename CreateInfo, typename Dispatch,
	vk::UniqueHandle<Type, Dispatch>(vk::Device::* createResourceUnique)(
		const CreateInfo&, vk::Optional<const vk::AllocationCallbacks>, const Dispatch&) const,
	void(vk::Device::* bindResourceMemory)(
		Type, vk::DeviceMemory, vk::DeviceSize, const Dispatch&) const>
ManagedResource<Type, CreateInfo, Dispatch, createResourceUnique, bindResourceMemory>::ManagedResource(
	vk::UniqueHandle<Type, Dispatch> resource, vk::ArrayProxy<char> data, vk::Device device, DeviceMemoryManager* dmm)
	: resource(std::move(resource))
	, device(device)
	, dmm(dmm)
{
	auto& memReqs = device.getBufferMemoryRequirements(resource.get());
	memBlock = dmm->allocateResource(memReqs, DeviceMemoryManager::MemoryUsage::Buffer);
	uploadData(data, memReqs);
}

template<typename Type, typename CreateInfo, typename Dispatch,
	vk::UniqueHandle<Type, Dispatch>(vk::Device::* createResourceUnique)(
		const CreateInfo&, vk::Optional<const vk::AllocationCallbacks>, const Dispatch&) const,
	void(vk::Device::* bindResourceMemory)(
		Type, vk::DeviceMemory, vk::DeviceSize, const Dispatch&) const>
ManagedResource<Type, CreateInfo, Dispatch, createResourceUnique, bindResourceMemory>::ManagedResource(
	const CreateInfo& resourceCreateInfo, vk::ArrayProxy<char> data, vk::Device device, DeviceMemoryManager* dmm)
	: device(device)
	, dmm(dmm)
{
	resource = (device.*createResourceUnique)(resourceCreateInfo, nullptr, vk::getDispatchLoaderStatic());
	auto& memReqs = device.getBufferMemoryRequirements(resource.get());
	memBlock = dmm->allocateResource(memReqs, MemoryUsage::UniformBuffer);
	uploadData(data, memReqs);
}

template<typename Type, typename CreateInfo, typename Dispatch,
	vk::UniqueHandle<Type, Dispatch>(vk::Device::* createResourceUnique)(
		const CreateInfo&, vk::Optional<const vk::AllocationCallbacks>, const Dispatch&) const,
	void(vk::Device::* bindResourceMemory)(
		Type, vk::DeviceMemory, vk::DeviceSize, const Dispatch&) const>
void ManagedResource<Type, CreateInfo, Dispatch, createResourceUnique, bindResourceMemory>::uploadData(
	vk::ArrayProxy<char> data, vk::MemoryRequirements& memReqs)
{
	// Copy buffer contents
	(device.*bindResourceMemory)(resource.get(), memBlock->memory, memBlock->offset, vk::getDispatchLoaderStatic());
	char* mappedMemory = (char*)device.mapMemory(memBlock->memory, memBlock->offset, memReqs.size);
	memcpy(mappedMemory, data.data(), data.size());
	device.unmapMemory(memBlock->memory);
}

using Image = ManagedResource<vk::Image, vk::ImageCreateInfo, vk::DispatchLoaderStatic, &vk::Device::createImageUnique, &vk::Device::bindImageMemory>;
using Buffer = ManagedResource<vk::Buffer, vk::BufferCreateInfo, vk::DispatchLoaderStatic, &vk::Device::createBufferUnique, &vk::Device::bindBufferMemory>;

}