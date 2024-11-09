#include <vulkan/vulkan.hpp>

#include <filesystem>
#include <string>
#include <map>

namespace vkrt {

class Shader {
	
public:
	Shader(const vk::Device& device, const std::string& shaderSrcFileName, std::string entryPoint = "main");
	Shader(const vk::Device& device, const std::string& shaderSrcFileName, vk::ShaderStageFlagBits shaderStage, std::string entryPoint = "main");
	~Shader() = default;

	vk::UniqueShaderModule shaderModule;
	vk::PipelineShaderStageCreateInfo shaderStageInfo;

private:
	static const std::map<std::string, vk::ShaderStageFlagBits> extensionToShaderType;

};

}