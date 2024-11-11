#include <raytracer.h>

namespace vkrt {

const std::vector<const char*> Raytracer::raytracingRequiredExtensions{
	vk::KHRAccelerationStructureExtensionName, vk::KHRDeferredHostOperationsExtensionName, vk::KHRRayTracingPipelineExtensionName
};

Raytracer::Raytracer(char* test)
	: Application("Vulkan raytracer", 800, 600, vk::ApiVersion12,
				  nullptr, nullptr, raytracingRequiredExtensions,
				  true, false, false, 3u,
				  vk::ImageUsageFlagBits::eTransferDst, { vk::Format::eB8G8R8A8Srgb },
				  { vk::PresentModeKHR::eMailbox, vk::PresentModeKHR::eFifo })
{
	createCommandPools();
	for (int i = 0; i < 3; i++) {
		imData[i] = (char*)malloc(width * height * 4);
		for (int j = 0; j < width * height; j++) {
			imData[i][4 * j + 0] = (char)(i == 0 ? 255 : 0);
			imData[i][4 * j + 1] = (char)(i == 1 ? 255 : 0);
			imData[i][4 * j + 2] = (char)(i == 2 ? 255 : 0);
			imData[i][4 * j + 3] = (char)(255);
		}
		auto& bufferCI = vk::BufferCreateInfo{}
			.setSize(width * height * 4)
			.setUsage(vk::BufferUsageFlagBits::eTransferSrc)
			.setQueueFamilyIndices(std::get<uint32_t>(graphicsQueue));
		buffer[i] = std::make_unique<Buffer>(bufferCI, vk::ArrayProxy(width * height * 4, imData[i]), device.get(), dmm.get());
	}

	for (int i = 0; i < framesInFlight; i++) {
		auto& cmdBfrCI = vk::CommandBufferAllocateInfo{}
			.setCommandPool(commandPool.get())
			.setCommandBufferCount(1u)
			.setLevel(vk::CommandBufferLevel::ePrimary);
		auto& cmdBfr = cmdBfrs[i];
		cmdBfrs[i] = std::move(device->allocateCommandBuffersUnique(cmdBfrCI)[0]);
	}
}

void Raytracer::createCommandPools() {
	auto& commandPoolCI = vk::CommandPoolCreateInfo{}
		.setQueueFamilyIndex(std::get<uint32_t>(graphicsQueue))
		.setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
	commandPool = device->createCommandPoolUnique(commandPoolCI);
}

void Raytracer::drawFrame(uint32_t frameIdx, vk::Semaphore imageAcquiredSemaphore, vk::Semaphore renderFinishedSemaphore,
						  vk::Fence frameFinishedFence) {
	auto& cmdBfr = cmdBfrs[frameIdx];
	cmdBfr->reset();

	cmdBfr->begin(vk::CommandBufferBeginInfo{}.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
	auto& preImMemBarrier = vk::ImageMemoryBarrier{}
		.setDstAccessMask(vk::AccessFlagBits::eTransferWrite)
		.setOldLayout(vk::ImageLayout::eUndefined)
		.setNewLayout(vk::ImageLayout::eTransferDstOptimal)
		.setImage(swapchainImages[frameIdx])
		.setSubresourceRange({ vk::ImageAspectFlagBits::eColor, 0u, 1u, 0u, 1u });
	cmdBfr->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer,
							{}, {}, {}, preImMemBarrier);

	auto& bfrImCp = vk::BufferImageCopy{}
		.setBufferOffset(0u)
		.setBufferRowLength(width)
		.setBufferImageHeight(height)
		.setImageSubresource({ vk::ImageAspectFlagBits::eColor, 0u, 0u, 1u })
		.setImageOffset({ 0u, 0u, 0u })
		.setImageExtent({ width, height, 1u });
	cmdBfr->copyBufferToImage(static_cast<vk::Buffer>(*(buffer[frameIdx])), swapchainImages[frameIdx], vk::ImageLayout::eTransferDstOptimal, bfrImCp);

	auto& postImMemBarrier = vk::ImageMemoryBarrier{}
		.setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
		.setOldLayout(vk::ImageLayout::eTransferDstOptimal)
		.setNewLayout(vk::ImageLayout::ePresentSrcKHR)
		.setImage(swapchainImages[frameIdx])
		.setSubresourceRange({ vk::ImageAspectFlagBits::eColor, 0u, 1u, 0u, 1u });
	cmdBfr->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer,
							{}, {}, {}, postImMemBarrier);
	cmdBfr->end();

	auto& waitDstStageMask = vk::PipelineStageFlags{ vk::PipelineStageFlagBits::eBottomOfPipe };
	auto& submitInfo = vk::SubmitInfo{}
		.setCommandBuffers(cmdBfr.get())
		.setWaitSemaphores(imageAcquiredSemaphore)
		.setSignalSemaphores(renderFinishedSemaphore)
		.setWaitDstStageMask(waitDstStageMask);
	std::get<vk::Queue>(graphicsQueue).submit(submitInfo, frameFinishedFence);
}

}