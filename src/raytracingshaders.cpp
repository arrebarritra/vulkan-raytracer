#include <raytracingshaders.h>

vkrt::RaytracingShaders::RaytracingShaders(vk::SharedDevice device, std::vector<std::string> raygenShaders, std::vector<std::string> missShaders, std::vector<std::array<std::string, 3>> hitGroups)
	: device(device)
{
	std::transform(raygenShaders.begin(), raygenShaders.end(), std::back_inserter(this->raygenShaders),
				   [device](const std::string& shaderPath) { return std::make_unique<Shader>(device, shaderPath); });
	std::transform(missShaders.begin(), missShaders.end(), std::back_inserter(this->missShaders),
				   [device](const std::string& shaderPath) { return std::make_unique<Shader>(device, shaderPath); });
	for (const auto& hg : hitGroups) {
		this->hitGroups.push_back({});
		if (!hg[0].empty()) this->hitGroups.back().closestHitShader = std::make_unique<Shader>(device, hg[0]);
		if (!hg[1].empty()) this->hitGroups.back().anyHitShader = std::make_unique<Shader>(device, hg[1]);
		if (!hg[2].empty()) this->hitGroups.back().intersectionShader = std::make_unique<Shader>(device, hg[2]);
	}

	generateShaderStages();
	generateShaderGroups();

	raygenGroupOffset = 0u;
	missGroupOffset = raygenShaders.size();
	hitGroupsOffset = missGroupOffset + missShaders.size();
}

void vkrt::RaytracingShaders::generateShaderStages() {
	shaderStages.reserve(raygenShaders.size() + missShaders.size() + 3 * hitGroups.size());
	std::transform(raygenShaders.begin(), raygenShaders.end(), std::back_inserter(shaderStages), [](const std::unique_ptr<Shader>& s) { return s->shaderStageInfo; });
	std::transform(missShaders.begin(), missShaders.end(), std::back_inserter(shaderStages), [](const std::unique_ptr<Shader>& s) { return s->shaderStageInfo; });
	for (const auto& hg : hitGroups) {
		if (hg.closestHitShader) shaderStages.push_back(hg.closestHitShader->shaderStageInfo);
		if (hg.anyHitShader) shaderStages.push_back(hg.anyHitShader->shaderStageInfo);
		if (hg.intersectionShader) shaderStages.push_back(hg.intersectionShader->shaderStageInfo);
	}
}

void vkrt::RaytracingShaders::generateShaderGroups() {
	shaderGroups.reserve(raygenShaders.size() + missShaders.size() + hitGroups.size());
	uint32_t shaderIdx = 0u;
	std::transform(raygenShaders.begin(), raygenShaders.end(), std::back_inserter(shaderGroups),
				   [&](const std::unique_ptr<Shader>& s) { return vk::RayTracingShaderGroupCreateInfoKHR{}.setType(vk::RayTracingShaderGroupTypeKHR::eGeneral).setGeneralShader(shaderIdx++); });
	std::transform(missShaders.begin(), missShaders.end(), std::back_inserter(shaderGroups),
				   [&](const std::unique_ptr<Shader>& s) { return vk::RayTracingShaderGroupCreateInfoKHR{}.setType(vk::RayTracingShaderGroupTypeKHR::eGeneral).setGeneralShader(shaderIdx++); });
	std::transform(hitGroups.begin(), hitGroups.end(), std::back_inserter(shaderGroups),
				   [&](const HitGroup& hg) {
					   return vk::RayTracingShaderGroupCreateInfoKHR{}
						   .setType(vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup)
						   .setClosestHitShader(hg.closestHitShader ? shaderIdx++ : vk::ShaderUnusedKHR)
						   .setAnyHitShader(hg.anyHitShader ? shaderIdx++ : vk::ShaderUnusedKHR)
						   .setIntersectionShader(hg.intersectionShader ? shaderIdx++ : vk::ShaderUnusedKHR);
				   });
}
