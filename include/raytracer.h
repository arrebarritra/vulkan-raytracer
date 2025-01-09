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
	struct CameraProperties {
		glm::mat4 viewInverse, projInverse;
	};
	CameraProperties camProps;

	struct PathTracingProperties {
		uint32_t sampleCount, maxRayDepth = 5u;
	};
	PathTracingProperties pathTracingProps;

	static const std::vector<const char*> raytracingRequiredExtensions;
	static const void* raytracingFeaturesChain;
	static const uint32_t FRAMES_IN_FLIGHT = 1u;

	vk::PhysicalDeviceRayTracingPipelinePropertiesKHR raytracingPipelineProperties;

	vk::UniqueCommandPool commandPool;
	std::vector<vk::UniqueCommandBuffer> raytraceCmdBuffers;

	Scene scene;
	std::unique_ptr<AccelerationStructure> as;
	std::unique_ptr<Image> storageImage;
	vk::UniqueImageView storageImageView;
	std::unique_ptr<Buffer> uniformCameraProps, uniformPathTracingProps;

	// Ray tracing pipeline
	vk::UniqueDescriptorSetLayout descriptorSetLayout;
	vk::UniquePipelineLayout raytracingPipelineLayout;
	vk::UniquePipeline raytracingPipeline;
	std::vector<vk::RayTracingShaderGroupCreateInfoKHR> shaderGroups;
	std::unique_ptr<Buffer> raygenShaderBindingTable, missShaderBindingTable, hitShaderBindingTable;

	vk::UniqueDescriptorPool descriptorPool;
	vk::DescriptorSet descriptorSet;

	std::vector<std::unique_ptr<Shader>> raygenShaders, missShaders, hitShaders;

	std::array<vk::SharedSemaphore, FRAMES_IN_FLIGHT> raytraceFinishedSemaphore;

	void createCommandPools() override;
	void createRaytracingPipeline();
	void createShaderBindingTable();
	void createDescriptorSets();
	void updateDescriptorSets();
	void recordCommandbuffer(uint32_t frameIdx);

	void handleResize() override;
	void drawFrame(uint32_t imageIdx, uint32_t frameIdx, vk::SharedSemaphore imageAcquiredSemaphore, vk::SharedSemaphore renderFinishedSemaphore,
				   vk::SharedFence frameFinishedFence) override;
};

}