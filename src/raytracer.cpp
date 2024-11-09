#include <raytracer.h>

namespace vkrt {

const std::vector<const char*> Raytracer::raytracingRequiredExtensions{
	vk::KHRAccelerationStructureExtensionName, vk::KHRDeferredHostOperationsExtensionName, vk::KHRRayTracingPipelineExtensionName
};
const vk::QueueFlags Raytracer::requiredQueues = vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer;
std::array<vk::QueueFlags, 1> preferExclusiveQueues{ vk::QueueFlagBits::eTransfer };

Raytracer::Raytracer(char* test)
	: Application("Vulkan raytracer", 800, 600, vk::ApiVersion12,
				  nullptr, nullptr, raytracingRequiredExtensions,
				  true, false, false, 3u,
				  vk::ImageUsageFlagBits::eTransferDst, { vk::Format::eB8G8R8Srgb, vk::Format::eB8G8R8A8Srgb },
				  { vk::PresentModeKHR::eMailbox, vk::PresentModeKHR::eFifo })
{

}

void Raytracer::drawFrame(uint32_t frameIdx, vk::Semaphore imageAcquiredSemaphore, vk::Semaphore renderFinishedSemaphore,
						  vk::Fence frameFinishedFence) {
	auto& waitDstStageMask = vk::PipelineStageFlags{ vk::PipelineStageFlagBits::eBottomOfPipe };
	auto& submitInfo = vk::SubmitInfo{}
		.setCommandBuffers(nullptr)
		.setWaitSemaphores(imageAcquiredSemaphore)
		.setSignalSemaphores(renderFinishedSemaphore)
		.setWaitDstStageMask(waitDstStageMask);
	graphicsQueue.submit(submitInfo, frameFinishedFence);
}

}