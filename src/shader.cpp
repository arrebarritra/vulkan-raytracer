#include <shader.h>
#include <logging.h>
#include <utils.h>

#include <fstream>
#include <ios>

namespace vkrt {

const std::map<std::string, vk::ShaderStageFlagBits> Shader::extensionToShaderType = {
	{".vert" , vk::ShaderStageFlagBits::eVertex},
	{".frag" , vk::ShaderStageFlagBits::eFragment},
	{".comp" , vk::ShaderStageFlagBits::eCompute},
	{".geom" , vk::ShaderStageFlagBits::eGeometry},
	{".tesc" , vk::ShaderStageFlagBits::eTessellationControl},
	{".tese" , vk::ShaderStageFlagBits::eTessellationEvaluation},
	{".mesh" , vk::ShaderStageFlagBits::eMeshEXT},
	{".task" , vk::ShaderStageFlagBits::eTaskEXT},
	{".rgen" , vk::ShaderStageFlagBits::eRaygenKHR},
	{".rchit", vk::ShaderStageFlagBits::eClosestHitKHR},
	{".rahit", vk::ShaderStageFlagBits::eAnyHitKHR},
	{".rmiss", vk::ShaderStageFlagBits::eMissKHR},
	{".rint", vk::ShaderStageFlagBits::eIntersectionKHR}
};

Shader::Shader(vk::SharedDevice device, const std::string& shaderSrcFileName, const char* entryPoint)
	: Shader(device, shaderSrcFileName,
			 extensionToShaderType.at(std::filesystem::path(shaderSrcFileName).extension().string()),
			 entryPoint) {}


Shader::Shader(vk::SharedDevice device, const std::string& shaderSrcFileName, vk::ShaderStageFlagBits shaderStage, const char* entryPoint) {
	std::ifstream stream(std::filesystem::path(SHADER_BINARY_DIR) / (shaderSrcFileName + ".spv"), std::ios::ate | std::ios::binary);
	size_t fileSize = stream.tellg();
	std::vector<uint32_t> binary((fileSize + sizeof(uint32_t) - 1) / sizeof(uint32_t));
	stream.seekg(0);
	stream.read(reinterpret_cast<char*>(binary.data()), fileSize);

	auto& shaderCI = vk::ShaderModuleCreateInfo{}.setCode(binary);
	shaderModule = device->createShaderModuleUnique(shaderCI);

	shaderStageInfo = vk::PipelineShaderStageCreateInfo{}
		.setStage(shaderStage)
		.setModule(shaderModule.get())
		.setPName(entryPoint);
}

}