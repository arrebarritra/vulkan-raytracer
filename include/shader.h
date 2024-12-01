#include <vulkan_headers.h>

#include <filesystem>
#include <string>
#include <map>

namespace vkrt {

class Shader {
	
public:
	Shader(const vk::SharedDevice device, const std::string& shaderSrcFileName, const char* entryPoint = "main");
	Shader(const vk::SharedDevice device, const std::string& shaderSrcFileName, vk::ShaderStageFlagBits shaderStage, const char* entryPoint = "main");
	~Shader() = default;

	vk::UniqueShaderModule shaderModule;
	vk::PipelineShaderStageCreateInfo shaderStageInfo;

private:
	static const std::map<std::string, vk::ShaderStageFlagBits> extensionToShaderType;

};

}