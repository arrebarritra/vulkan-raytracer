#include <raytracer.h>

namespace vkrt {

const std::vector<const char*> Raytracer::raytracingRequiredExtensions{
	vk::KHRAccelerationStructureExtensionName, vk::KHRDeferredHostOperationsExtensionName, vk::KHRRayTracingPipelineExtensionName
};

Raytracer::Raytracer(char* test)
	: Application("Vulkan raytracer", 800, 600, vk::ApiVersion12,
				  nullptr, nullptr, raytracingRequiredExtensions,
				  true, true, false, 3u,
				  vk::ImageUsageFlagBits::eTransferDst, { vk::Format::eB8G8R8A8Srgb },
				  { vk::PresentModeKHR::eMailbox, vk::PresentModeKHR::eFifo })
{
	createCommandPools();

	for (int i = 0; i < 3; i++) {
		imData[i] = (char*)malloc(width * height * 4);
		for (int j = 0; j < width * height; j++) {
			imData[i][4 * j + 0] = (char)(i == 2 ? 255 : 0);
			imData[i][4 * j + 1] = (char)(i == 1 ? 255 : 0);
			imData[i][4 * j + 2] = (char)(i == 0 ? 255 : 0);
			imData[i][4 * j + 3] = (char)(255);
		}
		auto& bufferCI = vk::BufferCreateInfo{}
			.setSize(width * height * 4)
			.setUsage(vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eStorageBuffer)
			.setQueueFamilyIndices(std::get<uint32_t>(*transferQueue));
		buffer[i] = std::make_unique<Buffer>(device, *dmm, *rch, bufferCI, vk::ArrayProxyNoTemporaries{ width * height * 4, imData[i] }, MemoryStorage::DevicePersistent);
	}
	
	device->waitForFences(*rch->submit(nullptr, nullptr, std::nullopt), vk::True, std::numeric_limits<uint64_t>::max());
}

void Raytracer::createCommandPools() {
	auto& commandPoolCI = vk::CommandPoolCreateInfo{}
		.setQueueFamilyIndex(std::get<uint32_t>(graphicsQueue))
		.setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
	commandPool = device->createCommandPoolUnique(commandPoolCI);
}

void Raytracer::drawFrame(uint32_t frameIdx, vk::SharedSemaphore imageAcquiredSemaphore, vk::SharedSemaphore renderFinishedSemaphore,
						  vk::SharedFence frameFinishedFence) {
	auto& bfrImCp = vk::BufferImageCopy{}
		.setBufferOffset(0u)
		.setBufferRowLength(width)
		.setBufferImageHeight(height)
		.setImageSubresource({ vk::ImageAspectFlagBits::eColor, 0u, 0u, 1u })
		.setImageOffset({ 0u, 0u, 0u })
		.setImageExtent({ width, height, 1u });
	buffer[frameIdx]->copyTo(swapchainImages[frameIdx], bfrImCp, vk::ImageLayout::ePresentSrcKHR);
	rch->submit(imageAcquiredSemaphore, renderFinishedSemaphore, frameFinishedFence);
}

}