#include <raytracer.h>
#include <utils.h>
#include <glm/gtc/matrix_transform.hpp>

namespace vkrt {

const std::vector<const char*> Raytracer::raytracingRequiredExtensions{
	vk::KHRAccelerationStructureExtensionName,
	vk::KHRRayTracingPipelineExtensionName,
	vk::KHRDeferredHostOperationsExtensionName,
	vk::KHRSpirv14ExtensionName,
	vk::KHRShaderFloatControlsExtensionName,
	vk::EXTDescriptorIndexingExtensionName
};

auto asFeatures = vk::PhysicalDeviceAccelerationStructureFeaturesKHR{}.setAccelerationStructure(vk::True);
auto rtpFeatures = vk::PhysicalDeviceRayTracingPipelineFeaturesKHR{}.setRayTracingPipeline(vk::True).setPNext(&asFeatures);
const void* Raytracer::raytracingFeaturesChain = &rtpFeatures;

Raytracer::Raytracer()
	: Application("Vulkan raytracer", 800, 600, vk::ApiVersion11,
				  nullptr, nullptr, raytracingRequiredExtensions, raytracingFeaturesChain,
				  true, false, false, FRAMES_IN_FLIGHT,
				  vk::ImageUsageFlagBits::eTransferDst, { vk::Format::eB8G8R8A8Srgb },
				  { vk::PresentModeKHR::eMailbox, vk::PresentModeKHR::eFifo })
{
	auto pdPropsTemp = vk::PhysicalDeviceProperties2({}, &raytracingPipelineProperties);
	physicalDevice.getProperties2(&pdPropsTemp);

	createCommandPools();
	auto cmdBufferAI = vk::CommandBufferAllocateInfo{}
		.setCommandPool(*commandPool)
		.setCommandBufferCount(framesInFlight);
	raytraceCmdBuffers = device->allocateCommandBuffersUnique(cmdBufferAI);

	// Create acceleration structure
	glm::vec3 vertices[3]{ glm::vec3(1.0f, 1.0f, 0.0f), glm::vec3(-1.0f, 1.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f) };
	uint32_t indices[3] = { 0u, 1u, 2u };
	scene.meshPool.emplace_back(device, *dmm, *rch, vertices, indices);
	scene.addObject(&scene.root, glm::mat4(1.0f), 0);

	CHECK_VULKAN_RESULT(device->waitForFences({ *scene.meshPool.back().vertices->writeFinishedFence, *scene.meshPool.back().indices->writeFinishedFence },
											  vk::True, std::numeric_limits<uint64_t>::max()));
	as = std::make_unique<AccelerationStructure>(device, *dmm, *rch, scene, graphicsQueue);

	// Create storage image and view
	auto storageImageCI = vk::ImageCreateInfo{}
		.setImageType(vk::ImageType::e2D)
		.setFormat(vk::Format::eB8G8R8A8Unorm)
		.setExtent(vk::Extent3D{ width, height, 1u })
		.setMipLevels(1u)
		.setArrayLayers(1u)
		.setSamples(vk::SampleCountFlagBits::e1)
		.setTiling(vk::ImageTiling::eOptimal)
		.setUsage(vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc)
		.setInitialLayout(vk::ImageLayout::eUndefined);
	storageImage = std::make_unique<Image>(device, *dmm, *rch, storageImageCI, nullptr, MemoryStorage::DevicePersistent);

	auto storageImViewCI = vk::ImageViewCreateInfo{}
		.setViewType(vk::ImageViewType::e2D)
		.setFormat(vk::Format::eB8G8R8A8Unorm)
		.setSubresourceRange(vk::ImageSubresourceRange{}
							 .setAspectMask(vk::ImageAspectFlagBits::eColor)
							 .setBaseMipLevel(0u)
							 .setLevelCount(1u)
							 .setBaseArrayLayer(0u)
							 .setLayerCount(1u))
		.setImage(**storageImage);
	storageImageView = device->createImageViewUnique(storageImViewCI);

	// Create and fill uniform buffer
	std::array matrices = { glm::mat4(1.0f), glm::mat4(1.0f) };
	auto bufferCI = vk::BufferCreateInfo{}
		.setSize(sizeof(matrices))
		.setUsage(vk::BufferUsageFlagBits::eUniformBuffer);
	uniformBuffer = std::make_unique<Buffer>(device, *dmm, *rch, bufferCI, vk::ArrayProxyNoTemporaries{ sizeof(matrices), (char*)matrices.data() }, MemoryStorage::DeviceDynamic);

	// Create resources
	createRaytracingPipeline();
	createShaderBindingTable();
	createDescriptorSets();
}

void Raytracer::createCommandPools() {
	auto commandPoolCI = vk::CommandPoolCreateInfo{}
		.setQueueFamilyIndex(std::get<uint32_t>(graphicsQueue))
		.setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
	commandPool = device->createCommandPoolUnique(commandPoolCI);
}

void Raytracer::createRaytracingPipeline() {
	auto accelerationStructureLB = vk::DescriptorSetLayoutBinding{}
		.setBinding(0u)
		.setDescriptorType(vk::DescriptorType::eAccelerationStructureKHR)
		.setDescriptorCount(1u)
		.setStageFlags(vk::ShaderStageFlagBits::eRaygenKHR);
	auto resultImageLB = vk::DescriptorSetLayoutBinding{}
		.setBinding(1u)
		.setDescriptorType(vk::DescriptorType::eStorageImage)
		.setDescriptorCount(1u)
		.setStageFlags(vk::ShaderStageFlagBits::eRaygenKHR);
	auto uniformBufferLB = vk::DescriptorSetLayoutBinding{}
		.setBinding(2u)
		.setDescriptorType(vk::DescriptorType::eUniformBuffer)
		.setDescriptorCount(1u)
		.setStageFlags(vk::ShaderStageFlagBits::eRaygenKHR);
	std::array bindings = { accelerationStructureLB, resultImageLB, uniformBufferLB };

	auto descriptorSetLayoutCI = vk::DescriptorSetLayoutCreateInfo{}.setBindings(bindings);
	descriptorSetLayout = device->createDescriptorSetLayoutUnique(descriptorSetLayoutCI);

	auto pipelineLayoutCI = vk::PipelineLayoutCreateInfo{}.setSetLayouts(*descriptorSetLayout);
	raytracingPipelineLayout = device->createPipelineLayoutUnique(pipelineLayoutCI);

	// Shaders
	raygen = std::make_unique<Shader>(device, "raygen.rgen");
	miss = std::make_unique<Shader>(device, "miss.rmiss");
	hit = std::make_unique<Shader>(device, "hit.rchit");
	std::array shaderStages = { raygen->shaderStageInfo, miss->shaderStageInfo, hit->shaderStageInfo };

	shaderGroups = { vk::RayTracingShaderGroupCreateInfoKHR{}.setType(vk::RayTracingShaderGroupTypeKHR::eGeneral).setGeneralShader(0),
					 vk::RayTracingShaderGroupCreateInfoKHR{}.setType(vk::RayTracingShaderGroupTypeKHR::eGeneral).setGeneralShader(1),
					 vk::RayTracingShaderGroupCreateInfoKHR{}.setType(vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup).setGeneralShader(2) };

	auto raytracingPipelineCI = vk::RayTracingPipelineCreateInfoKHR{}
		.setStages(shaderStages)
		.setGroups(shaderGroups)
		.setMaxPipelineRayRecursionDepth(1u)
		.setLayout(*raytracingPipelineLayout);
	auto raytracingPipelineRV = device->createRayTracingPipelineKHRUnique(nullptr, nullptr, raytracingPipelineCI);
	EXIT_ON_VULKAN_NON_SUCCESS(raytracingPipelineRV.result);
	raytracingPipeline = std::move(raytracingPipelineRV.value);
}

void Raytracer::createShaderBindingTable() {
	uint32_t handleSize = utils::alignedOffset(raytracingPipelineProperties.shaderGroupHandleSize, raytracingPipelineProperties.shaderGroupHandleAlignment);
	auto shaderGroupHandles = device->getRayTracingShaderGroupHandlesKHR<char>(*raytracingPipeline, 0u, shaderGroups.size() * sizeof(char), shaderGroups.size() * handleSize);
	auto bufferCI = vk::BufferCreateInfo{}
		.setUsage(vk::BufferUsageFlagBits::eShaderBindingTableKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress)
		.setSize(handleSize);
	raygenShaderBindingTable = std::make_unique<Buffer>(device, *dmm, *rch, bufferCI, shaderGroupHandles[0], MemoryStorage::DeviceDynamic);
	missShaderBindingTable = std::make_unique<Buffer>(device, *dmm, *rch, bufferCI, shaderGroupHandles[1], MemoryStorage::DeviceDynamic);
	hitShaderBindingTable = std::make_unique<Buffer>(device, *dmm, *rch, bufferCI, shaderGroupHandles[2], MemoryStorage::DeviceDynamic);
}

void Raytracer::createDescriptorSets() {
	std::array poolSizes = { vk::DescriptorPoolSize{vk::DescriptorType::eAccelerationStructureKHR, 1},
							 vk::DescriptorPoolSize{vk::DescriptorType::eStorageImage, 1},
							 vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, 1} };

	auto descriptorPoolCI = vk::DescriptorPoolCreateInfo{}
		.setPoolSizes(poolSizes)
		.setMaxSets(1u);
	descriptorPool = device->createDescriptorPoolUnique(descriptorPoolCI);

	auto descriptorSetAI = vk::DescriptorSetAllocateInfo{}
		.setDescriptorPool(*descriptorPool)
		.setDescriptorSetCount(1u)
		.setSetLayouts(*descriptorSetLayout);
	descriptorSet = device->allocateDescriptorSets(descriptorSetAI).front();
}

void Raytracer::updateDescriptorSets() {
	// Writes
	auto writeDescriptorAccelerationStructure = vk::WriteDescriptorSetAccelerationStructureKHR{}.setAccelerationStructures(*as->tlas);
	auto accelerationStructureWrite = vk::WriteDescriptorSet{}
		.setPNext(&writeDescriptorAccelerationStructure)
		.setDstSet(descriptorSet)
		.setDstBinding(0u)
		.setDescriptorCount(1u)
		.setDescriptorType(vk::DescriptorType::eAccelerationStructureKHR);

	auto storageImageDescriptor = vk::DescriptorImageInfo{}
		.setImageView(*storageImageView)
		.setImageLayout(vk::ImageLayout::eGeneral);
	auto resultImageWrite = vk::WriteDescriptorSet{}
		.setDstSet(descriptorSet)
		.setDstBinding(1u)
		.setImageInfo(storageImageDescriptor)
		.setDescriptorType(vk::DescriptorType::eStorageImage);

	auto uniformBufferDescriptor = vk::DescriptorBufferInfo{}
		.setBuffer(**uniformBuffer)
		.setRange(uniformBuffer->bufferCI.size);
	auto uniformBufferWrite = vk::WriteDescriptorSet{}
		.setDstSet(descriptorSet)
		.setDstBinding(2u)
		.setBufferInfo(uniformBufferDescriptor)
		.setDescriptorType(vk::DescriptorType::eUniformBuffer);

	descriptorWrites = { accelerationStructureWrite, resultImageWrite, uniformBufferWrite };
	device->updateDescriptorSets(descriptorWrites, nullptr);
}

void Raytracer::recordCommandbuffer(uint32_t idx) {
	auto& cmdBuffer = raytraceCmdBuffers[idx];
	cmdBuffer->reset();
	cmdBuffer->begin(vk::CommandBufferBeginInfo{}.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

	uint32_t handleSize = utils::alignedOffset(raytracingPipelineProperties.shaderGroupHandleSize, raytracingPipelineProperties.shaderGroupHandleAlignment);
	auto raygenSBTEntry = vk::StridedDeviceAddressRegionKHR{}
		.setDeviceAddress(device->getBufferAddress(**raygenShaderBindingTable))
		.setSize(handleSize)
		.setStride(handleSize);
	auto missSBTEntry = vk::StridedDeviceAddressRegionKHR{}
		.setDeviceAddress(device->getBufferAddress(**missShaderBindingTable))
		.setSize(handleSize)
		.setStride(handleSize);
	auto hitSBTEntry = vk::StridedDeviceAddressRegionKHR{}
		.setDeviceAddress(device->getBufferAddress(**hitShaderBindingTable))
		.setSize(handleSize)
		.setStride(handleSize);
	vk::StridedDeviceAddressRegionKHR callableSBTEntry;

	auto storageImgMemBarrier = vk::ImageMemoryBarrier{}
		.setDstAccessMask(vk::AccessFlagBits::eShaderWrite)
		.setOldLayout(vk::ImageLayout::eUndefined)
		.setNewLayout(vk::ImageLayout::eGeneral)
		.setImage(**storageImage)
		.setSubresourceRange({ vk::ImageAspectFlagBits::eColor, 0u, 1u, 0u, 1u });
	cmdBuffer->pipelineBarrier(vk::PipelineStageFlagBits::eBottomOfPipe, vk::PipelineStageFlagBits::eRayTracingShaderKHR,
							   {}, {}, {}, storageImgMemBarrier);

	cmdBuffer->bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, *raytracingPipeline);
	cmdBuffer->bindDescriptorSets(vk::PipelineBindPoint::eRayTracingKHR, *raytracingPipelineLayout, 0u, descriptorSet, nullptr);
	cmdBuffer->traceRaysKHR(raygenSBTEntry, missSBTEntry, hitSBTEntry, callableSBTEntry, width, height, 1u);

	cmdBuffer->end();
}

void Raytracer::drawFrame(uint32_t frameIdx, vk::SharedSemaphore imageAcquiredSemaphore, vk::SharedSemaphore renderFinishedSemaphore,
						  vk::SharedFence frameFinishedFence) {
	recordCommandbuffer(frameIdx);
	raytraceFinishedSemaphore[frameIdx] = vk::SharedHandle(device->createSemaphore({}), device);

	auto waitDstStage = vk::PipelineStageFlags(vk::PipelineStageFlagBits::eRayTracingShaderKHR);
	auto signalSemaphore = *raytraceFinishedSemaphore[frameIdx];
	auto submitInfo = vk::SubmitInfo{}
		.setCommandBuffers(*raytraceCmdBuffers[frameIdx])
		.setSignalSemaphores(signalSemaphore);
	std::get<vk::Queue>(graphicsQueue).submit(submitInfo);

	auto imgCp = vk::ImageCopy{}
		.setSrcSubresource(vk::ImageSubresourceLayers{}
						   .setAspectMask(vk::ImageAspectFlagBits::eColor)
						   .setBaseArrayLayer(0u)
						   .setLayerCount(1u)
						   .setMipLevel(0u))
		.setDstSubresource(vk::ImageSubresourceLayers{}
						   .setAspectMask(vk::ImageAspectFlagBits::eColor)
						   .setBaseArrayLayer(0u)
						   .setLayerCount(1u)
						   .setMipLevel(0u))
		.setExtent(vk::Extent3D{ width, height, 1u });

	storageImage->copyTo(swapchainImages[frameIdx], imgCp, vk::ImageLayout::ePresentSrcKHR);
	std::array waitSemaphores = { imageAcquiredSemaphore, raytraceFinishedSemaphore[frameIdx] };
	rch->submit(waitSemaphores, renderFinishedSemaphore, frameFinishedFence);
}

}