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

	char* imData[3];
	std::unique_ptr<Buffer> buffer[3];


	void createCommandPools() override;
	void drawFrame(uint32_t frameIdx, vk::SharedSemaphore imageAcquiredSemaphore, vk::SharedSemaphore renderFinishedSemaphore,
		   vk::SharedFence frameFinishedFence) override;
};

}