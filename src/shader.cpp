#include <shader.h>
#include <logging.h>

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

Shader::Shader(const vk::Device& device, const std::string& shaderSrcFileName, std::string entryPoint)
	: Shader(device, shaderSrcFileName,
			 extensionToShaderType.at(std::filesystem::path(shaderSrcFileName).extension().string()),
			 entryPoint) {}


Shader::Shader(const vk::Device& device, const std::string& shaderSrcFileName, vk::ShaderStageFlagBits shaderStage, std::string entryPoint)
{
	std::ifstream stream(std::filesystem::path(SHADER_BINARY_DIR) / (shaderSrcFileName + ".spv"), std::ios::ate | std::ios::binary);
	size_t fileSize = stream.tellg();
	std::vector<uint32_t> binary(fileSize);
	stream.seekg(0);
	stream.read(reinterpret_cast<char*>(binary.data()), fileSize);

	auto& shaderCI = vk::ShaderModuleCreateInfo{}
		.setCodeSize(binary.size())
		.setPCode(binary.data());
	shaderModule = device.createShaderModuleUnique(shaderCI);

	shaderStageInfo = vk::PipelineShaderStageCreateInfo{}
		.setStage(shaderStage)
		.setModule(shaderModule.get())
		.setPName(entryPoint.c_str());
}

}