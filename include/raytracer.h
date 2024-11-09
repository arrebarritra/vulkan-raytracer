#pragma once

#include <vulkan/vulkan.hpp>

#include <application.h>

namespace vkrt {

class Raytracer : public Application {
public:
	Raytracer(char* test);
	~Raytracer() = default;

private:
	static const std::vector<const char*> raytracingRequiredExtensions;
	static const vk::QueueFlags requiredQueues;

	std::array<vk::QueueFlags, 1> preferExclusiveQueues{ vk::QueueFlagBits::eTransfer };
	void drawFrame(uint32_t frameIdx, vk::Semaphore imageAcquiredSemaphore, vk::Semaphore renderFinishedSemaphore,
		   vk::Fence frameFinishedFence) override;
};

}