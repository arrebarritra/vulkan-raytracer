#pragma once

#include <vulkan_headers.h>

#include <shader.h>

namespace vkrt {

struct HitGroup {
	std::unique_ptr<Shader> closestHitShader, anyHitShader, intersectionShader;
};

class RaytracingShaders {
public:
	RaytracingShaders(vk::SharedDevice device, std::vector<std::string> raygenShaders, std::vector<std::string> missShaders, std::vector<std::array<std::string, 3>> hitGroups);
	
	std::vector<std::unique_ptr<Shader>> raygenShaders, missShaders;
	std::vector<HitGroup> hitGroups;
	uint32_t raygenGroupOffset, missGroupOffset, hitGroupsOffset;
	std::vector<vk::RayTracingShaderGroupCreateInfoKHR> shaderGroups;
	std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;

private:
	void generateShaderStages();
	void generateShaderGroups();

	vk::SharedDevice device;
};

}