#pragma once

#include <application.h>
#include <scene.h>
#include <mesh.h>
#include <accelerationstructure.h>
#include <shader.h>

namespace vkrt {

class Raytracer : public Application {
public:
	Raytracer();
	~Raytracer() = default;

private:
	static const std::vector<const char*> raytracingRequiredExtensions;
	static const void* raytracingFeaturesChain;
	static const uint32_t FRAMES_IN_FLIGHT = 3u;

	vk::PhysicalDeviceRayTracingPipelinePropertiesKHR raytracingPipelineProperties;

	vk::UniqueCommandPool commandPool;
	std::vector<vk::UniqueCommandBuffer> raytraceCmdBuffers;

	char* imData[3];
	std::unique_ptr<Buffer> buffer[3];
	Scene scene;
	std::unique_ptr<AccelerationStructure> as;
	std::unique_ptr<Image> storageImage;
	vk::UniqueImageView storageImageView;
	std::unique_ptr<Buffer> uniformBuffer;

	// Ray tracing pipeline
	vk::UniqueDescriptorSetLayout descriptorSetLayout;
	vk::UniquePipelineLayout raytracingPipelineLayout;
	vk::UniquePipeline raytracingPipeline;
	std::vector<vk::RayTracingShaderGroupCreateInfoKHR> shaderGroups;
	std::unique_ptr<Buffer> raygenShaderBindingTable, missShaderBindingTable, hitShaderBindingTable;

	vk::UniqueDescriptorPool descriptorPool;
	vk::DescriptorSet descriptorSet;
	std::vector<vk::WriteDescriptorSet> descriptorWrites;

	std::unique_ptr<Shader> raygen, miss, hit;

	std::array<vk::SharedSemaphore, FRAMES_IN_FLIGHT> raytraceFinishedSemaphore;

	void createCommandPools() override;
	void createRaytracingPipeline();
	void createShaderBindingTable();
	void createDescriptorSets();
	void updateDescriptorSets();
	void recordCommandbuffer(uint32_t idx);

	void drawFrame(uint32_t frameIdx, vk::SharedSemaphore imageAcquiredSemaphore, vk::SharedSemaphore renderFinishedSemaphore,
				   vk::SharedFence frameFinishedFence) override;
};

}