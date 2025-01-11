#include <raytracer.h>
#include <utils.h>
#include <camera.h>
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

auto diFeatures = vk::PhysicalDeviceDescriptorIndexingFeaturesEXT{}
.setShaderSampledImageArrayNonUniformIndexing(vk::True)
.setRuntimeDescriptorArray(vk::True)
.setDescriptorBindingVariableDescriptorCount(vk::True);
auto asFeatures = vk::PhysicalDeviceAccelerationStructureFeaturesKHR{}.setAccelerationStructure(vk::True).setPNext(&diFeatures);
auto rtpFeatures = vk::PhysicalDeviceRayTracingPipelineFeaturesKHR{}.setRayTracingPipeline(vk::True).setPNext(&asFeatures);
const void* Raytracer::raytracingFeaturesChain = &rtpFeatures;

Raytracer::Raytracer()
	: Application("Vulkan raytracer", 1280, 720, vk::ApiVersion11,
				  nullptr, nullptr, raytracingRequiredExtensions, raytracingFeaturesChain,
				  true, false, false, FRAMES_IN_FLIGHT,
				  vk::ImageUsageFlagBits::eTransferDst, { vk::Format::eB8G8R8A8Srgb },
				  { vk::PresentModeKHR::eMailbox, vk::PresentModeKHR::eFifo })
	, scene(device, *dmm, *rth)
{
	//glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	//if (glfwRawMouseMotionSupported())
	//	glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);

	auto pdPropsTemp = vk::PhysicalDeviceProperties2({}, &raytracingPipelineProperties);
	physicalDevice.getProperties2(&pdPropsTemp);

	createCommandPools();
	auto cmdBufferAI = vk::CommandBufferAllocateInfo{}
		.setCommandPool(*commandPool)
		.setCommandBufferCount(framesInFlight);
	raytraceCmdBuffers = device->allocateCommandBuffersUnique(cmdBufferAI);

	createImages();

	// Create acceleration structure
	scene.loadModel(RESOURCE_DIR"NewSponza_Main_glTF_003.gltf", &scene.root);
	scene.loadModel(RESOURCE_DIR"NewSponza_Curtains_glTF.gltf", &scene.root);
	scene.uploadResources();
	rth->flushPendingTransfers();

	as = std::make_unique<AccelerationStructure>(device, *dmm, *rth, scene, graphicsQueue);
	rth->flushPendingTransfers();

	// Upload uniforms
	camProps = CameraProperties{ camera.getViewInv(), camera.getProjectionInv() };
	auto uniformCameraPropsCI = vk::BufferCreateInfo{}
		.setSize(sizeof(CameraProperties))
		.setUsage(vk::BufferUsageFlagBits::eUniformBuffer);
	uniformCameraProps = std::make_unique<Buffer>(device, *dmm, *rth, uniformCameraPropsCI, vk::ArrayProxyNoTemporaries{ sizeof(CameraProperties), (char*)&camProps }, MemoryStorage::DeviceDynamic);

	pathTracingProps.sampleCount = 0u;
	auto uniformPathTracingPropsCI = vk::BufferCreateInfo{}
		.setSize(sizeof(PathTracingProperties))
		.setUsage(vk::BufferUsageFlagBits::eUniformBuffer);
	uniformPathTracingProps = std::make_unique<Buffer>(device, *dmm, *rth, uniformPathTracingPropsCI, vk::ArrayProxyNoTemporaries{ sizeof(PathTracingProperties), (char*)&pathTracingProps }, MemoryStorage::DeviceDynamic);

	// Create resources
	createRaytracingPipeline();
	createShaderBindingTable();
	createDescriptorSets();
	updateDescriptorSets();
	rth->flushPendingTransfers();

	glfwShowWindow(window);
}

void Raytracer::createCommandPools() {
	auto commandPoolCI = vk::CommandPoolCreateInfo{}
		.setQueueFamilyIndex(std::get<uint32_t>(graphicsQueue))
		.setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
	commandPool = device->createCommandPoolUnique(commandPoolCI);
}

void Raytracer::createImages() {
	// Create accumulation and storage images and image views
	auto outputImageCI = vk::ImageCreateInfo{}
		.setImageType(vk::ImageType::e2D)
		.setFormat(vk::Format::eB8G8R8A8Unorm)
		.setExtent(vk::Extent3D{ width, height, 1u })
		.setMipLevels(1u)
		.setArrayLayers(1u)
		.setSamples(vk::SampleCountFlagBits::e1)
		.setTiling(vk::ImageTiling::eOptimal)
		.setUsage(vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc)
		.setInitialLayout(vk::ImageLayout::eUndefined);
	outputImage = std::make_unique<Image>(device, *dmm, *rth, outputImageCI, nullptr, vk::ImageLayout::eUndefined, MemoryStorage::DevicePersistent);
	auto outputImViewCI = vk::ImageViewCreateInfo{}
		.setViewType(vk::ImageViewType::e2D)
		.setFormat(vk::Format::eB8G8R8A8Unorm)
		.setSubresourceRange(vk::ImageSubresourceRange{}
							 .setAspectMask(vk::ImageAspectFlagBits::eColor)
							 .setBaseMipLevel(0u)
							 .setLevelCount(1u)
							 .setBaseArrayLayer(0u)
							 .setLayerCount(1u))
		.setImage(**outputImage);
	outputImageView = device->createImageViewUnique(outputImViewCI);

	auto accumulationImageCI = vk::ImageCreateInfo{}
		.setImageType(vk::ImageType::e2D)
		.setFormat(vk::Format::eR32G32B32A32Sfloat)
		.setExtent(vk::Extent3D{ width, height, 1u })
		.setMipLevels(1u)
		.setArrayLayers(1u)
		.setSamples(vk::SampleCountFlagBits::e1)
		.setTiling(vk::ImageTiling::eOptimal)
		.setUsage(vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc)
		.setInitialLayout(vk::ImageLayout::eUndefined);
	accumulationImage = std::make_unique<Image>(device, *dmm, *rth, accumulationImageCI, nullptr, vk::ImageLayout::eUndefined, MemoryStorage::DevicePersistent);
	auto accumulationImViewCI = vk::ImageViewCreateInfo{}
		.setViewType(vk::ImageViewType::e2D)
		.setFormat(vk::Format::eR32G32B32A32Sfloat)
		.setSubresourceRange(vk::ImageSubresourceRange{}
							 .setAspectMask(vk::ImageAspectFlagBits::eColor)
							 .setBaseMipLevel(0u)
							 .setLevelCount(1u)
							 .setBaseArrayLayer(0u)
							 .setLayerCount(1u))
		.setImage(**accumulationImage);
	accumulationImageView = device->createImageViewUnique(accumulationImViewCI);
}


void Raytracer::createRaytracingPipeline() {
	auto accelerationStructureLB = vk::DescriptorSetLayoutBinding{}
		.setBinding(0u)
		.setDescriptorType(vk::DescriptorType::eAccelerationStructureKHR)
		.setDescriptorCount(1u)
		.setStageFlags(vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eClosestHitKHR);
	auto accumulationImageLB = vk::DescriptorSetLayoutBinding{}
		.setBinding(1u)
		.setDescriptorType(vk::DescriptorType::eStorageImage)
		.setDescriptorCount(1u)
		.setStageFlags(vk::ShaderStageFlagBits::eRaygenKHR);
	auto outputImageLB = vk::DescriptorSetLayoutBinding{}
		.setBinding(2u)
		.setDescriptorType(vk::DescriptorType::eStorageImage)
		.setDescriptorCount(1u)
		.setStageFlags(vk::ShaderStageFlagBits::eRaygenKHR);
	auto uniformCameraPropsLB = vk::DescriptorSetLayoutBinding{}
		.setBinding(3u)
		.setDescriptorType(vk::DescriptorType::eUniformBuffer)
		.setDescriptorCount(1u)
		.setStageFlags(vk::ShaderStageFlagBits::eRaygenKHR);
	auto uniformPathTracingPropsLB = vk::DescriptorSetLayoutBinding{}
		.setBinding(4u)
		.setDescriptorType(vk::DescriptorType::eUniformBuffer)
		.setDescriptorCount(1u)
		.setStageFlags(vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eAnyHitKHR);
	auto geometryInfoBufferLB = vk::DescriptorSetLayoutBinding{}
		.setBinding(5u)
		.setDescriptorType(vk::DescriptorType::eStorageBuffer)
		.setDescriptorCount(1u)
		.setStageFlags(vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eAnyHitKHR);
	auto materialsBufferLB = vk::DescriptorSetLayoutBinding{}
		.setBinding(6u)
		.setDescriptorType(vk::DescriptorType::eStorageBuffer)
		.setDescriptorCount(1u)
		.setStageFlags(vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eAnyHitKHR);
	auto pointLightsBufferLB = vk::DescriptorSetLayoutBinding{}
		.setBinding(7u)
		.setDescriptorType(vk::DescriptorType::eStorageBuffer)
		.setDescriptorCount(1u)
		.setStageFlags(vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eAnyHitKHR);
	auto directionalLightsBufferLB = vk::DescriptorSetLayoutBinding{}
		.setBinding(8u)
		.setDescriptorType(vk::DescriptorType::eStorageBuffer)
		.setDescriptorCount(1u)
		.setStageFlags(vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eAnyHitKHR);
	auto textureSamplersLB = vk::DescriptorSetLayoutBinding{}
		.setBinding(9u)
		.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
		.setDescriptorCount(static_cast<uint32_t>(scene.texturePool.size()))
		.setStageFlags(vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eAnyHitKHR);
	std::array layoutBindings = { accelerationStructureLB, accumulationImageLB, outputImageLB, uniformCameraPropsLB,
									uniformPathTracingPropsLB, geometryInfoBufferLB, materialsBufferLB,
									pointLightsBufferLB, directionalLightsBufferLB, textureSamplersLB };

	std::array descriptorBindingFlags = {
		vk::DescriptorBindingFlagsEXT{},
		vk::DescriptorBindingFlagsEXT{},
		vk::DescriptorBindingFlagsEXT{},
		vk::DescriptorBindingFlagsEXT{},
		vk::DescriptorBindingFlagsEXT{},
		vk::DescriptorBindingFlagsEXT{},
		vk::DescriptorBindingFlagsEXT{},
		vk::DescriptorBindingFlagsEXT{},
		vk::DescriptorBindingFlagsEXT{},
		vk::DescriptorBindingFlags{vk::DescriptorBindingFlagBitsEXT::eVariableDescriptorCount}
	};
	auto layoutBindingFlags = vk::DescriptorSetLayoutBindingFlagsCreateInfoEXT{}.setBindingFlags(descriptorBindingFlags);

	auto descriptorSetLayoutCI = vk::DescriptorSetLayoutCreateInfo{}
		.setPNext(&layoutBindingFlags)
		.setBindings(layoutBindings);
	descriptorSetLayout = device->createDescriptorSetLayoutUnique(descriptorSetLayoutCI);

	auto pipelineLayoutCI = vk::PipelineLayoutCreateInfo{}.setSetLayouts(*descriptorSetLayout);
	raytracingPipelineLayout = device->createPipelineLayoutUnique(pipelineLayoutCI);

	// Shaders
	std::vector<std::string> raygenShaders = { "raygen.rgen" };
	std::vector<std::string> missShaders = { "miss.rmiss", "shadow.rmiss" };
	std::vector<std::array<std::string, 3>> hitGroups = { {"hit.rchit", "anyhit.rahit", ""} };
	raytracingShaders = std::make_unique<RaytracingShaders>(device, raygenShaders, missShaders, hitGroups);

	// Create shader groups
	auto raytracingPipelineCI = vk::RayTracingPipelineCreateInfoKHR{}
		.setStages(raytracingShaders->shaderStages)
		.setGroups(raytracingShaders->shaderGroups)
		.setMaxPipelineRayRecursionDepth(1u)
		.setLayout(*raytracingPipelineLayout);
	auto raytracingPipelineRV = device->createRayTracingPipelineKHRUnique(nullptr, nullptr, raytracingPipelineCI);
	EXIT_ON_VULKAN_NON_SUCCESS(raytracingPipelineRV.result);
	raytracingPipeline = std::move(raytracingPipelineRV.value);
}

void Raytracer::createShaderBindingTable() {
	uint32_t handleSize = utils::alignedSize(raytracingPipelineProperties.shaderGroupHandleSize, raytracingPipelineProperties.shaderGroupHandleAlignment);
	auto shaderGroupHandles = device->getRayTracingShaderGroupHandlesKHR<char>(*raytracingPipeline, 0u, raytracingShaders->shaderGroups.size(), raytracingShaders->shaderGroups.size() * handleSize);

	auto raygenSBTCI = vk::BufferCreateInfo{}
		.setUsage(vk::BufferUsageFlagBits::eShaderBindingTableKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress)
		.setSize(raytracingShaders->raygenShaders.size() * handleSize);
	raygenShaderBindingTable = std::make_unique<Buffer>(device, *dmm, *rth, raygenSBTCI,
														vk::ArrayProxyNoTemporaries{ static_cast<uint32_t>(raytracingShaders->raygenShaders.size() * handleSize),  raytracingShaders->raygenGroupOffset + shaderGroupHandles.data() },
														MemoryStorage::DeviceDynamic);
	auto missSBTCI = vk::BufferCreateInfo{}
		.setUsage(vk::BufferUsageFlagBits::eShaderBindingTableKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress)
		.setSize(raytracingShaders->missShaders.size() * handleSize);
	missShaderBindingTable = std::make_unique<Buffer>(device, *dmm, *rth, missSBTCI,
													  vk::ArrayProxyNoTemporaries{ static_cast<uint32_t>(raytracingShaders->missShaders.size() * handleSize), raytracingShaders->missGroupOffset * handleSize + shaderGroupHandles.data() },
													  MemoryStorage::DeviceDynamic);
	auto hitSBTCI = vk::BufferCreateInfo{}
		.setUsage(vk::BufferUsageFlagBits::eShaderBindingTableKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress)
		.setSize(raytracingShaders->hitGroups.size() * handleSize);
	hitShaderBindingTable = std::make_unique<Buffer>(device, *dmm, *rth, hitSBTCI,
													 vk::ArrayProxyNoTemporaries{ static_cast<uint32_t>(raytracingShaders->hitGroups.size() * handleSize), raytracingShaders->hitGroupsOffset * handleSize + shaderGroupHandles.data() },
													 MemoryStorage::DeviceDynamic);
}

void Raytracer::createDescriptorSets() {
	uint32_t textureCount = static_cast<uint32_t>(scene.texturePool.size());
	std::array poolSizes = { vk::DescriptorPoolSize{vk::DescriptorType::eAccelerationStructureKHR, 1},
							 vk::DescriptorPoolSize{vk::DescriptorType::eStorageImage, 1},
							 vk::DescriptorPoolSize{vk::DescriptorType::eStorageImage, 1},
							 vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, 1},
							 vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, 1},
							 vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, 1},
							 vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, 1},
							 vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, 1},
							 vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, 1},
							 vk::DescriptorPoolSize{vk::DescriptorType::eCombinedImageSampler, 1}
	};

	auto descriptorPoolCI = vk::DescriptorPoolCreateInfo{}
		.setPoolSizes(poolSizes)
		.setMaxSets(1u);
	descriptorPool = device->createDescriptorPoolUnique(descriptorPoolCI);
	auto variableDescriptorCountAI = vk::DescriptorSetVariableDescriptorCountAllocateInfoEXT{}.setDescriptorCounts(textureCount);
	auto descriptorSetAI = vk::DescriptorSetAllocateInfo{}
		.setPNext(&variableDescriptorCountAI)
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

	auto accumulationImageDescriptor = vk::DescriptorImageInfo{}
		.setImageView(*accumulationImageView)
		.setImageLayout(vk::ImageLayout::eGeneral);
	auto accumulationImageWrite = vk::WriteDescriptorSet{}
		.setDstSet(descriptorSet)
		.setDstBinding(1u)
		.setImageInfo(accumulationImageDescriptor)
		.setDescriptorType(vk::DescriptorType::eStorageImage);

	auto outputImageDescriptor = vk::DescriptorImageInfo{}
		.setImageView(*outputImageView)
		.setImageLayout(vk::ImageLayout::eGeneral);
	auto outputImageWrite = vk::WriteDescriptorSet{}
		.setDstSet(descriptorSet)
		.setDstBinding(2u)
		.setImageInfo(outputImageDescriptor)
		.setDescriptorType(vk::DescriptorType::eStorageImage);

	auto uniformCameraPropsDescriptor = vk::DescriptorBufferInfo{}
		.setBuffer(**uniformCameraProps)
		.setRange(uniformCameraProps->bufferCI.size);
	auto uniformCameraPropsWrite = vk::WriteDescriptorSet{}
		.setDstSet(descriptorSet)
		.setDstBinding(3u)
		.setBufferInfo(uniformCameraPropsDescriptor)
		.setDescriptorType(vk::DescriptorType::eUniformBuffer);

	auto uniformPathTracingPropsDescriptor = vk::DescriptorBufferInfo{}
		.setBuffer(**uniformPathTracingProps)
		.setRange(uniformPathTracingProps->bufferCI.size);
	auto uniformPathTracingPropsWrite = vk::WriteDescriptorSet{}
		.setDstSet(descriptorSet)
		.setDstBinding(4u)
		.setBufferInfo(uniformPathTracingPropsDescriptor)
		.setDescriptorType(vk::DescriptorType::eUniformBuffer);

	auto geometryInfoBufferDescriptor = vk::DescriptorBufferInfo{}
		.setBuffer(**scene.geometryInfoBuffer)
		.setRange(scene.geometryInfoBuffer->bufferCI.size);
	auto geometryInfoBufferWrite = vk::WriteDescriptorSet{}
		.setDstSet(descriptorSet)
		.setDstBinding(5u)
		.setBufferInfo(geometryInfoBufferDescriptor)
		.setDescriptorType(vk::DescriptorType::eStorageBuffer);

	auto materialsBufferDescriptor = vk::DescriptorBufferInfo{}
		.setBuffer(**scene.materialsBuffer)
		.setRange(scene.materialsBuffer->bufferCI.size);
	auto materialsBufferWrite = vk::WriteDescriptorSet{}
		.setDstSet(descriptorSet)
		.setDstBinding(6u)
		.setBufferInfo(materialsBufferDescriptor)
		.setDescriptorType(vk::DescriptorType::eStorageBuffer);

	auto pointLightsBufferDescriptor = vk::DescriptorBufferInfo{}
		.setBuffer(**scene.pointLightsBuffer)
		.setRange(scene.pointLightsBuffer->bufferCI.size);
	auto pointLightsBufferWrite = vk::WriteDescriptorSet{}
		.setDstSet(descriptorSet)
		.setDstBinding(7u)
		.setBufferInfo(pointLightsBufferDescriptor)
		.setDescriptorType(vk::DescriptorType::eStorageBuffer);

	auto directionalLightsBufferDescriptor = vk::DescriptorBufferInfo{}
		.setBuffer(**scene.directionalLightsBuffer)
		.setRange(scene.directionalLightsBuffer->bufferCI.size);
	auto directionalLightsBufferWrite = vk::WriteDescriptorSet{}
		.setDstSet(descriptorSet)
		.setDstBinding(8u)
		.setBufferInfo(directionalLightsBufferDescriptor)
		.setDescriptorType(vk::DescriptorType::eStorageBuffer);

	std::vector<vk::WriteDescriptorSet> descriptorWrites = {
		accelerationStructureWrite, accumulationImageWrite, outputImageWrite,
		uniformCameraPropsWrite, uniformPathTracingPropsWrite,geometryInfoBufferWrite, materialsBufferWrite,
		pointLightsBufferWrite, directionalLightsBufferWrite
	};

	std::vector<vk::DescriptorImageInfo> textureDescriptors;
	if (scene.texturePool.size() > 0) {
		textureDescriptors.reserve(scene.texturePool.size());
		for (const auto& texture : scene.texturePool) {
			textureDescriptors.push_back(texture->getDescriptor());
		}
		auto& textureWrites = vk::WriteDescriptorSet{}
			.setDstSet(descriptorSet)
			.setDstBinding(9u)
			.setImageInfo(textureDescriptors)
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler);

		descriptorWrites.push_back(textureWrites);
	}

	device->updateDescriptorSets(descriptorWrites, nullptr);
}

void Raytracer::recordCommandbuffer(uint32_t frameIdx) {
	auto& cmdBuffer = raytraceCmdBuffers[frameIdx];
	cmdBuffer->reset();
	cmdBuffer->begin(vk::CommandBufferBeginInfo{}.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

	uint32_t handleSize = utils::alignedSize(raytracingPipelineProperties.shaderGroupHandleSize, raytracingPipelineProperties.shaderGroupHandleAlignment);
	auto raygenSBTEntry = vk::StridedDeviceAddressRegionKHR{}
		.setDeviceAddress(device->getBufferAddress(**raygenShaderBindingTable))
		.setSize(raytracingShaders->raygenShaders.size() * handleSize)
		.setStride(handleSize);
	auto missSBTEntry = vk::StridedDeviceAddressRegionKHR{}
		.setDeviceAddress(device->getBufferAddress(**missShaderBindingTable))
		.setSize(raytracingShaders->missShaders.size() * handleSize)
		.setStride(handleSize);
	auto hitSBTEntry = vk::StridedDeviceAddressRegionKHR{}
		.setDeviceAddress(device->getBufferAddress(**hitShaderBindingTable))
		.setSize(raytracingShaders->hitGroups.size() * handleSize)
		.setStride(handleSize);
	vk::StridedDeviceAddressRegionKHR callableSBTEntry;

	auto accumulationImgMemBarrier = vk::ImageMemoryBarrier{}
		.setDstAccessMask(vk::AccessFlagBits::eShaderWrite)
		.setOldLayout(vk::ImageLayout::eUndefined)
		.setNewLayout(vk::ImageLayout::eGeneral)
		.setImage(**accumulationImage)
		.setSubresourceRange({ vk::ImageAspectFlagBits::eColor, 0u, 1u, 0u, 1u });
	auto outputImgMemBarrier = vk::ImageMemoryBarrier{}
		.setDstAccessMask(vk::AccessFlagBits::eShaderWrite)
		.setOldLayout(vk::ImageLayout::eUndefined)
		.setNewLayout(vk::ImageLayout::eGeneral)
		.setImage(**outputImage)
		.setSubresourceRange({ vk::ImageAspectFlagBits::eColor, 0u, 1u, 0u, 1u });
	cmdBuffer->pipelineBarrier(vk::PipelineStageFlagBits::eBottomOfPipe, vk::PipelineStageFlagBits::eRayTracingShaderKHR,
							   {}, {}, {}, { accumulationImgMemBarrier, outputImgMemBarrier });

	cmdBuffer->bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, *raytracingPipeline);
	cmdBuffer->bindDescriptorSets(vk::PipelineBindPoint::eRayTracingKHR, *raytracingPipelineLayout, 0u, descriptorSet, nullptr);
	cmdBuffer->traceRaysKHR(raygenSBTEntry, missSBTEntry, hitSBTEntry, callableSBTEntry, width, height, 1u);

	cmdBuffer->end();
}

void Raytracer::handleResize() {
	Application::handleResize();
	if (minimised) return;
	createImages();
	updateDescriptorSets();
	pathTracingProps.sampleCount = 0u;
}

void Raytracer::drawFrame(uint32_t imageidx, uint32_t frameIdx, vk::SharedSemaphore imageAcquiredSemaphore, vk::SharedSemaphore renderFinishedSemaphore,
						  vk::SharedFence frameFinishedFence) {
	if (camera.positionChanged || camera.directionChanged) pathTracingProps.sampleCount = 0u;
	camProps = CameraProperties{ camera.getViewInv(), camera.getProjectionInv() };
	uniformCameraProps->write(vk::ArrayProxyNoTemporaries{ sizeof(CameraProperties), (char*)&camProps });
	uniformPathTracingProps->write(vk::ArrayProxyNoTemporaries{ sizeof(PathTracingProperties), (char*)&pathTracingProps });

	recordCommandbuffer(frameIdx);
	raytraceFinishedSemaphore[frameIdx] = vk::SharedHandle(device->createSemaphore({}), device);

	auto waitDstStage = vk::PipelineStageFlags(vk::PipelineStageFlagBits::eRayTracingShaderKHR);
	auto signalSemaphore = *raytraceFinishedSemaphore[frameIdx];
	auto submitInfo = vk::SubmitInfo{}
		.setCommandBuffers(*raytraceCmdBuffers[frameIdx])
		.setSignalSemaphores(signalSemaphore);
	std::get<vk::Queue>(graphicsQueue).submit(submitInfo, minimised ? *frameFinishedFence : vk::Fence{});

	if (!minimised) {
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
		SyncInfo storageToSwapchainSI{ frameFinishedFence, { imageAcquiredSemaphore, raytraceFinishedSemaphore[frameIdx] }, { renderFinishedSemaphore } };
		outputImage->copyTo(swapchainImages[imageidx], imgCp, vk::ImageLayout::ePresentSrcKHR, std::move(storageToSwapchainSI));
	}
	pathTracingProps.sampleCount++;
}

}