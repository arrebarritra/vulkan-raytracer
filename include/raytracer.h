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

	vk::UniqueCommandPool commandPool;
	std::array<vk::UniqueCommandBuffer, 3> cmdBfrs;

	char* imData[3];
	std::unique_ptr<Buffer> buffer[3];


	void createCommandPools() override;
	void drawFrame(uint32_t frameIdx, vk::Semaphore imageAcquiredSemaphore, vk::Semaphore renderFinishedSemaphore,
		   vk::Fence frameFinishedFence) override;
};

}