#include <raytracer.h>

namespace vkrt {

const std::vector<const char*> Raytracer::raytracingRequiredExtensions{
	vk::KHRAccelerationStructureExtensionName, vk::KHRDeferredHostOperationsExtensionName, vk::KHRRayTracingPipelineExtensionName
};
const vk::QueueFlags Raytracer::requiredQueues = vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer;
std::array<vk::QueueFlags, 1> preferExclusiveQueues{ vk::QueueFlagBits::eTransfer };

Raytracer::Raytracer(char* test)
	: Application("Vulkan raytracer", 800, 600, vk::ApiVersion12,
				  nullptr, nullptr, raytracingRequiredExtensions)
{

}

void Raytracer::drawFrame(uint32_t frameIdx, vk::Semaphore imageAcquiredSemaphore, vk::Semaphore renderFinishedSemaphore,
				  vk::Fence frameFinishedFence) {

}

}