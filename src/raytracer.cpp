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

	// Create acceleration structure
	scene.loadModel(nullptr, glm::mat4(1.0f), RESOURCE_DIR"sponzasmall/Sponza.gltf");
	rth->flushPendingTransfers();

	//CHECK_VULKAN_RESULT(device->waitForFences({ *scene.meshPool.back().vertices->writeFinishedFence, *scene.meshPool.back().indices->writeFinishedFence },
	//										  vk::True, std::numeric_limits<uint64_t>::max()));
	as = std::make_unique<AccelerationStructure>(device, *dmm, *rth, scene, graphicsQueue);
	rth->flushPendingTransfers();

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
	storageImage = std::make_unique<Image>(device, *dmm, *rth, storageImageCI, nullptr, vk::ImageLayout::eUndefined, MemoryStorage::DevicePersistent);

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

	UniformData uniformData{ sampleCount, camera.getViewInv(), camera.getProjectionInv() };
	auto bufferCI = vk::BufferCreateInfo{}
		.setSize(sizeof(UniformData))
		.setUsage(vk::BufferUsageFlagBits::eUniformBuffer);
	uniformBuffer = std::make_unique<Buffer>(device, *dmm, *rth, bufferCI, vk::ArrayProxyNoTemporaries{ sizeof(UniformData), (char*)&uniformData }, MemoryStorage::DeviceDynamic);

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

void Raytracer::createRaytracingPipeline() {
	auto accelerationStructureLB = vk::DescriptorSetLayoutBinding{}
		.setBinding(0u)
		.setDescriptorType(vk::DescriptorType::eAccelerationStructureKHR)
		.setDescriptorCount(1u)
		.setStageFlags(vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eAnyHitKHR);
	auto resultImageLB = vk::DescriptorSetLayoutBinding{}
		.setBinding(1u)
		.setDescriptorType(vk::DescriptorType::eStorageImage)
		.setDescriptorCount(1u)
		.setStageFlags(vk::ShaderStageFlagBits::eRaygenKHR);
	auto uniformBufferLB = vk::DescriptorSetLayoutBinding{}
		.setBinding(2u)
		.setDescriptorType(vk::DescriptorType::eUniformBuffer)
		.setDescriptorCount(1u)
		.setStageFlags(vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eClosestHitKHR);
	auto geometryInfoBufferLB = vk::DescriptorSetLayoutBinding{}
		.setBinding(3u)
		.setDescriptorType(vk::DescriptorType::eStorageBuffer)
		.setDescriptorCount(1u)
		.setStageFlags(vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eAnyHitKHR);
	auto materialsBufferLB = vk::DescriptorSetLayoutBinding{}
		.setBinding(4u)
		.setDescriptorType(vk::DescriptorType::eStorageBuffer)
		.setDescriptorCount(1u)
		.setStageFlags(vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eAnyHitKHR);
	auto pointLightsBufferLB = vk::DescriptorSetLayoutBinding{}
		.setBinding(5u)
		.setDescriptorType(vk::DescriptorType::eStorageBuffer)
		.setDescriptorCount(1u)
		.setStageFlags(vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eAnyHitKHR);
	auto directionalLightsBufferLB = vk::DescriptorSetLayoutBinding{}
		.setBinding(6u)
		.setDescriptorType(vk::DescriptorType::eStorageBuffer)
		.setDescriptorCount(1u)
		.setStageFlags(vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eAnyHitKHR);
	auto textureSamplersLB = vk::DescriptorSetLayoutBinding{}
		.setBinding(7u)
		.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
		.setDescriptorCount(static_cast<uint32_t>(scene.texturePool.size()))
		.setStageFlags(vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eAnyHitKHR);
	std::array layoutBindings = { accelerationStructureLB, resultImageLB, uniformBufferLB,
									geometryInfoBufferLB, materialsBufferLB,
									pointLightsBufferLB, directionalLightsBufferLB, textureSamplersLB };

	std::array<vk::DescriptorBindingFlagsEXT, 8> descriptorBindingFlags = {
		vk::DescriptorBindingFlagsEXT{},
		vk::DescriptorBindingFlagsEXT{},
		vk::DescriptorBindingFlagsEXT{},
		vk::DescriptorBindingFlagsEXT{},
		vk::DescriptorBindingFlagsEXT{},
		vk::DescriptorBindingFlagsEXT{},
		vk::DescriptorBindingFlagsEXT{},
		vk::DescriptorBindingFlagBitsEXT::eVariableDescriptorCount
	};
	auto layoutBindingFlags = vk::DescriptorSetLayoutBindingFlagsCreateInfoEXT{}.setBindingFlags(descriptorBindingFlags);

	auto descriptorSetLayoutCI = vk::DescriptorSetLayoutCreateInfo{}
		.setPNext(&layoutBindingFlags)
		.setBindings(layoutBindings);
	descriptorSetLayout = device->createDescriptorSetLayoutUnique(descriptorSetLayoutCI);

	auto pipelineLayoutCI = vk::PipelineLayoutCreateInfo{}.setSetLayouts(*descriptorSetLayout);
	raytracingPipelineLayout = device->createPipelineLayoutUnique(pipelineLayoutCI);

	// Shaders
	raygenShaders.emplace_back(std::make_unique<Shader>(device, "raygen.rgen"));
	missShaders.emplace_back(std::make_unique<Shader>(device, "miss.rmiss"));
	missShaders.emplace_back(std::make_unique<Shader>(device, "shadow.rmiss"));
	hitShaders.emplace_back(std::make_unique<Shader>(device, "hit.rchit"));

	// Set shader stages
	std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;
	shaderStages.reserve(raygenShaders.size() + missShaders.size() + hitShaders.size());
	std::transform(raygenShaders.begin(), raygenShaders.end(), std::back_inserter(shaderStages), [](const std::unique_ptr<Shader>& s) { return s->shaderStageInfo; });
	std::transform(missShaders.begin(), missShaders.end(), std::back_inserter(shaderStages), [](const std::unique_ptr<Shader>& s) { return s->shaderStageInfo; });
	std::transform(hitShaders.begin(), hitShaders.end(), std::back_inserter(shaderStages), [](const std::unique_ptr<Shader>& s) { return s->shaderStageInfo; });

	// Create shader groups
	std::transform(raygenShaders.begin(), raygenShaders.end(), std::back_inserter(shaderGroups),
				   [&](const std::unique_ptr<Shader>& s) { return vk::RayTracingShaderGroupCreateInfoKHR{}.setType(vk::RayTracingShaderGroupTypeKHR::eGeneral).setGeneralShader(shaderGroups.size()); });
	std::transform(missShaders.begin(), missShaders.end(), std::back_inserter(shaderGroups),
				   [&](const std::unique_ptr<Shader>& s) { return vk::RayTracingShaderGroupCreateInfoKHR{}.setType(vk::RayTracingShaderGroupTypeKHR::eGeneral).setGeneralShader(shaderGroups.size()); });
	std::transform(hitShaders.begin(), hitShaders.end(), std::back_inserter(shaderGroups),
				   [&](const std::unique_ptr<Shader>& s) { return vk::RayTracingShaderGroupCreateInfoKHR{}.setType(vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup).setClosestHitShader(shaderGroups.size()); });

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
	uint32_t handleSize = utils::alignedSize(raytracingPipelineProperties.shaderGroupHandleSize, raytracingPipelineProperties.shaderGroupHandleAlignment);
	auto shaderGroupHandles = device->getRayTracingShaderGroupHandlesKHR<char>(*raytracingPipeline, 0u, shaderGroups.size(), shaderGroups.size() * handleSize);

	auto raygenSBTCI = vk::BufferCreateInfo{}
		.setUsage(vk::BufferUsageFlagBits::eShaderBindingTableKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress)
		.setSize(raygenShaders.size() * handleSize);
	raygenShaderBindingTable = std::make_unique<Buffer>(device, *dmm, *rth, raygenSBTCI, vk::ArrayProxyNoTemporaries{ static_cast<uint32_t>(raygenShaders.size() * handleSize),  shaderGroupHandles.data() }, MemoryStorage::DeviceDynamic);

	auto missSBTCI = vk::BufferCreateInfo{}
		.setUsage(vk::BufferUsageFlagBits::eShaderBindingTableKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress)
		.setSize(missShaders.size() * handleSize);
	missShaderBindingTable = std::make_unique<Buffer>(device, *dmm, *rth, missSBTCI, vk::ArrayProxyNoTemporaries{ static_cast<uint32_t>(missShaders.size() * handleSize), shaderGroupHandles.data() + raygenShaders.size() * handleSize }, MemoryStorage::DeviceDynamic);

	auto hitSBTCI = vk::BufferCreateInfo{}
		.setUsage(vk::BufferUsageFlagBits::eShaderBindingTableKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress)
		.setSize(hitShaders.size() * handleSize);
	hitShaderBindingTable = std::make_unique<Buffer>(device, *dmm, *rth, hitSBTCI,
													 vk::ArrayProxyNoTemporaries{ static_cast<uint32_t>(hitShaders.size() * handleSize), shaderGroupHandles.data() + (raygenShaders.size() + missShaders.size()) * handleSize },
													 MemoryStorage::DeviceDynamic);
}

void Raytracer::createDescriptorSets() {
	uint32_t textureCount = static_cast<uint32_t>(scene.texturePool.size());
	std::array poolSizes = { vk::DescriptorPoolSize{vk::DescriptorType::eAccelerationStructureKHR, 1},
							 vk::DescriptorPoolSize{vk::DescriptorType::eStorageImage, 1},
							 vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, 1},
							 vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, 1},
							 vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, 1},
							 vk::DescriptorPoolSize{vk::DescriptorType::eCombinedImageSampler, textureCount},
							 vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, 1},
							 vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, 1}
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

	auto geometryInfoBufferDescriptor = vk::DescriptorBufferInfo{}
		.setBuffer(**scene.geometryInfoBuffer)
		.setRange(scene.geometryInfoBuffer->bufferCI.size);
	auto geometryInfoBufferWrite = vk::WriteDescriptorSet{}
		.setDstSet(descriptorSet)
		.setDstBinding(3u)
		.setBufferInfo(geometryInfoBufferDescriptor)
		.setDescriptorType(vk::DescriptorType::eStorageBuffer);

	auto materialsBufferDescriptor = vk::DescriptorBufferInfo{}
		.setBuffer(**scene.materialsBuffer)
		.setRange(scene.materialsBuffer->bufferCI.size);
	auto materialsBufferWrite = vk::WriteDescriptorSet{}
		.setDstSet(descriptorSet)
		.setDstBinding(4u)
		.setBufferInfo(materialsBufferDescriptor)
		.setDescriptorType(vk::DescriptorType::eStorageBuffer);

	auto pointLightsBufferDescriptor = vk::DescriptorBufferInfo{}
		.setBuffer(**scene.pointLightsBuffer)
		.setRange(scene.pointLightsBuffer->bufferCI.size);
	auto pointLightsBufferWrite = vk::WriteDescriptorSet{}
		.setDstSet(descriptorSet)
		.setDstBinding(5u)
		.setBufferInfo(pointLightsBufferDescriptor)
		.setDescriptorType(vk::DescriptorType::eStorageBuffer);

	auto directionalLightsBufferDescriptor = vk::DescriptorBufferInfo{}
		.setBuffer(**scene.directionalLightsBuffer)
		.setRange(scene.directionalLightsBuffer->bufferCI.size);
	auto directionalLightsBufferWrite = vk::WriteDescriptorSet{}
		.setDstSet(descriptorSet)
		.setDstBinding(6u)
		.setBufferInfo(directionalLightsBufferDescriptor)
		.setDescriptorType(vk::DescriptorType::eStorageBuffer);

	std::vector<vk::DescriptorImageInfo> textureDescriptors;
	textureDescriptors.reserve(scene.texturePool.size());
	for (const auto& texture : scene.texturePool) {
		textureDescriptors.push_back(texture->getDescriptor());
	}
	auto& textureWrites = vk::WriteDescriptorSet{}
		.setDstSet(descriptorSet)
		.setDstBinding(7u)
		.setImageInfo(textureDescriptors)
		.setDescriptorType(vk::DescriptorType::eCombinedImageSampler);

	descriptorWrites = { accelerationStructureWrite, resultImageWrite, uniformBufferWrite,
						 geometryInfoBufferWrite, materialsBufferWrite,
						 pointLightsBufferWrite, directionalLightsBufferWrite, textureWrites
	};
	device->updateDescriptorSets(descriptorWrites, nullptr);
}

void Raytracer::recordCommandbuffer(uint32_t frameIdx) {
	auto& cmdBuffer = raytraceCmdBuffers[frameIdx];
	cmdBuffer->reset();
	cmdBuffer->begin(vk::CommandBufferBeginInfo{}.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

	uint32_t handleSize = utils::alignedSize(raytracingPipelineProperties.shaderGroupHandleSize, raytracingPipelineProperties.shaderGroupHandleAlignment);
	auto raygenSBTEntry = vk::StridedDeviceAddressRegionKHR{}
		.setDeviceAddress(device->getBufferAddress(**raygenShaderBindingTable))
		.setSize(raygenShaders.size() * handleSize)
		.setStride(handleSize);
	auto missSBTEntry = vk::StridedDeviceAddressRegionKHR{}
		.setDeviceAddress(device->getBufferAddress(**missShaderBindingTable))
		.setSize(missShaders.size() * handleSize)
		.setStride(handleSize);
	auto hitSBTEntry = vk::StridedDeviceAddressRegionKHR{}
		.setDeviceAddress(device->getBufferAddress(**hitShaderBindingTable))
		.setSize(hitShaders.size() * handleSize)
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

void Raytracer::handleResize() {
	Application::handleResize();
	if (minimised) return;
	// Resize storage image
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
	storageImage = std::make_unique<Image>(device, *dmm, *rth, storageImageCI, nullptr, vk::ImageLayout::eUndefined, MemoryStorage::DevicePersistent);

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
	updateDescriptorSets();
	sampleCount = 0u;
}

void Raytracer::drawFrame(uint32_t imageidx, uint32_t frameIdx, vk::SharedSemaphore imageAcquiredSemaphore, vk::SharedSemaphore renderFinishedSemaphore,
						  vk::SharedFence frameFinishedFence) {
	if (camera.positionChanged || camera.directionChanged) sampleCount = 0u;
	UniformData uniformData = { sampleCount, camera.getViewInv(), camera.getProjectionInv() };
	uniformBuffer->write(vk::ArrayProxyNoTemporaries{ sizeof(UniformData), (char*)&uniformData });

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
		storageImage->copyTo(swapchainImages[imageidx], imgCp, vk::ImageLayout::ePresentSrcKHR, std::move(storageToSwapchainSI));
	}

	sampleCount++;
}

}