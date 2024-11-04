#include <iostream>
#include <fstream>
#include <string>
#include <array>
#include <vector>

#include <vulkan/vulkan.hpp>
#include <vulkan/vk_enum_string_helper.h>

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#define __FILENAME__ (__FILE__ + SOURCE_PATH_SIZE) \

#define CHECK_VULKAN_RESULT(res) \
if (vk::Result(res) != vk::Result::eSuccess) { \
	fprintf(stderr, "%s(%d): %s (code %d)\n", __FILENAME__, __LINE__, string_VkResult(static_cast<VkResult>(res)), res); \
}

#define EXIT_ON_VULKAN_ERROR(res) \
if (vk::Result(res) != vk::Result::eSuccess) { \
	fprintf(stderr, "%s(%d): %s (code %d)\n", __FILENAME__, __LINE__, string_VkResult(static_cast<VkResult>(res)), res); \
	throw std::runtime_error("Exited due to error");  \
}

int main() {
	if (!glfwInit()) {
		throw std::runtime_error("Could not initialise GLFW");
	}

	if (!glfwVulkanSupported()) {
		throw std::runtime_error("Vulkan not supported");
	}

	// Create instance
	std::vector<const char*> instExtensions;
	uint32_t glfwReqExtCount;
	auto glfwReqExt = glfwGetRequiredInstanceExtensions(&glfwReqExtCount);
	for (int i = 0; i < glfwReqExtCount; i++) instExtensions.push_back(glfwReqExt[i]);

	const std::vector<const char*> validationLayers{
#ifndef NDEBUG
		"VK_LAYER_KHRONOS_validation"
#endif
	};

	auto appInfo = vk::ApplicationInfo{}
		.setPApplicationName("Vulkan Raytracer")
		.setApiVersion(vk::ApiVersion12);
	auto instanceInfo = vk::InstanceCreateInfo{}
		.setPApplicationInfo(&appInfo)
		.setPEnabledExtensionNames(instExtensions)
		.setPEnabledLayerNames(validationLayers);
	auto instance = vk::createInstanceUnique(instanceInfo);

	// Create window
	const int WIDTH = 800;
	const int HEIGHT = 600;
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan Raytracer", nullptr, nullptr);


	// Create surface
	vk::UniqueSurfaceKHR surface;
	{
		VkSurfaceKHR surfaceTmp{};
		CHECK_VULKAN_RESULT(glfwCreateWindowSurface(instance.get(), window, nullptr, &surfaceTmp));
		surface = vk::UniqueSurfaceKHR(surfaceTmp, *instance);
	}


	// Select device and queue
	std::vector<const char*> devReqExtensions;
	devReqExtensions.push_back(vk::KHRSwapchainExtensionName);
	//devReqExtensions.insert(devReqExtensions.end(), { vk::KHRRayTracingPipelineExtensionName, vk::KHRAccelerationStructureExtensionName });
	std::sort(devReqExtensions.begin(), devReqExtensions.end(),
		[](const char* a, const char* b) {
			return strcmp(a, b) < 0;
		});
	auto reqQueueFlags = vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer;
	auto physicalDevices = instance->enumeratePhysicalDevices();
	uint32_t selectedQueueFamilyIndex;
	vk::PhysicalDevice physicalDevice;
	bool deviceSelected = false;

	for (const auto& pd : physicalDevices) {
		// Supports extensions
		const auto devExtensions = pd.enumerateDeviceExtensionProperties();
		std::vector<const char*> devExtensionNames(devExtensions.size());
		std::transform(devExtensions.begin(), devExtensions.end(), devExtensionNames.begin(),
			[](const vk::ExtensionProperties& ep) {
				return ep.extensionName.data();
			});
		std::sort(devExtensionNames.begin(), devExtensionNames.end(),
			[](const char* a, const char* b) {
				return strcmp(a, b) < 0;
			});

		bool extsSupported = std::includes(devExtensionNames.begin(), devExtensionNames.end(), devReqExtensions.begin(), devReqExtensions.end(),
			[](const char* a, const char* const& b) {
				return strcmp(a, b) == 0;
			});
		if (!extsSupported) continue;

		auto queueFamilyProperties = pd.getQueueFamilyProperties();
		selectedQueueFamilyIndex = -1u;
		bool queueReqSupported = false;
		for (const auto& qfp : queueFamilyProperties) {
			selectedQueueFamilyIndex++;
			// Supports required flags
			if (!(qfp.queueFlags & reqQueueFlags)) continue;

			// Supports presentation
			vk::Bool32 presSupported;
			pd.getSurfaceSupportKHR(selectedQueueFamilyIndex, surface.get(), &presSupported);
			if (!presSupported) continue;

			queueReqSupported = true;
			break;
		}
		if (!queueReqSupported) continue;

		physicalDevice = pd;
		deviceSelected = true;
		break;
	}
	if (!deviceSelected) throw std::runtime_error("Could not find physical device with required properties");

	// Create logical device
	std::array<float, 1> queuePriorities{ {1.0f} };
	auto& mainQueueInfo = vk::DeviceQueueCreateInfo{}
		.setQueueFamilyIndex(selectedQueueFamilyIndex)
		.setQueuePriorities(queuePriorities);
	std::array<vk::DeviceQueueCreateInfo, 1> queueCreateInfos{ {mainQueueInfo} };

	auto& deviceInfo = vk::DeviceCreateInfo{}
		.setPEnabledExtensionNames(devReqExtensions)
		.setQueueCreateInfos(queueCreateInfos);

	auto device = physicalDevice.createDeviceUnique(deviceInfo);

	// Get queue
	auto queue = device->getQueue(selectedQueueFamilyIndex, 0u);

	// Set up swapchain
	auto srfCpbts = physicalDevice.getSurfaceCapabilitiesKHR(surface.get());
	auto srfFmts = physicalDevice.getSurfaceFormatsKHR(surface.get());
	auto presModes = physicalDevice.getSurfacePresentModesKHR(surface.get());

	auto& srfFmt = *std::find_if(srfFmts.begin(), srfFmts.end(),
		[](const vk::SurfaceFormatKHR& fmt) {
			return fmt.format == vk::Format::eB8G8R8A8Srgb && fmt.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
		});
	auto& presMode = *std::find_if(presModes.begin(), presModes.end(),
		[](const vk::PresentModeKHR& pm) {
			return pm == vk::PresentModeKHR::eFifo;
		});


	int fbWidth, fbHeight;
	glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
	vk::Extent2D imgExtent(fbWidth, fbHeight);
	const uint32_t FRAMES_IN_FLIGHT = 3u;

	auto& swapchainInfo = vk::SwapchainCreateInfoKHR{}
		.setSurface(surface.get())
		.setImageExtent(imgExtent)
		.setMinImageCount(FRAMES_IN_FLIGHT)
		.setImageArrayLayers(1u)
		.setImageUsage(vk::ImageUsageFlagBits::eColorAttachment)
		.setImageFormat(srfFmt.format)
		.setImageColorSpace(srfFmt.colorSpace)
		.setPresentMode(presMode);

	auto swapchain = device->createSwapchainKHRUnique(swapchainInfo);
	std::array swapchains = { swapchain.get() };
	auto swapchainImages = device->getSwapchainImagesKHR(swapchain.get());
	std::vector<vk::UniqueImageView> swapchainImageViews(FRAMES_IN_FLIGHT);

	std::transform(swapchainImages.begin(), swapchainImages.end(), swapchainImageViews.begin(),
		[pDevice = device.get(), srfFmt](const vk::Image& im) mutable {
			auto imViewCreateInfo = vk::ImageViewCreateInfo{}
				.setImage(im)
				.setViewType(vk::ImageViewType::e2D)
				.setFormat(srfFmt.format)
				.setSubresourceRange(vk::ImageSubresourceRange{}
					.setBaseMipLevel(0u)
					.setLevelCount(1u)
					.setBaseArrayLayer(0u)
					.setLayerCount(1u)
					.setAspectMask(vk::ImageAspectFlagBits::eColor));
			return pDevice.createImageViewUnique(imViewCreateInfo);
		});

	// Load shaders
	std::ifstream vertFs(SHADER_BINARY_DIR"/test.vert.spv", std::ios::ate | std::ios::binary);
	size_t vertFileSize = vertFs.tellg();
	std::vector<uint32_t> vertShaderCode(vertFileSize);
	vertFs.seekg(0);
	vertFs.read(reinterpret_cast<char*>(vertShaderCode.data()), vertFileSize);

	std::ifstream fragFs(SHADER_BINARY_DIR"/test.frag.spv", std::ios::ate | std::ios::binary);
	size_t fragFileSize = fragFs.tellg();
	std::vector<uint32_t> fragShaderCode(fragFileSize);
	fragFs.seekg(0);
	fragFs.read(reinterpret_cast<char*>(fragShaderCode.data()), fragFileSize);

	auto& vShCreateInfo = vk::ShaderModuleCreateInfo{}
		.setCode(vertShaderCode)
		.setCodeSize(vertShaderCode.size()); // force size due to Vulkan-Hpp assigning size * 4
	auto& fShCreateInfo = vk::ShaderModuleCreateInfo{}
		.setCode(fragShaderCode)
		.setCodeSize(fragShaderCode.size()); // force size due to Vulkan-Hpp assigning size * 4

	auto vertShaderModule = device->createShaderModuleUnique(vShCreateInfo);
	auto fragShaderModule = device->createShaderModuleUnique(fShCreateInfo);

	auto& vertShaderStageInfo = vk::PipelineShaderStageCreateInfo{}
		.setStage(vk::ShaderStageFlagBits::eVertex)
		.setModule(vertShaderModule.get())
		.setPName("main");
	auto& fragShaderStageInfo = vk::PipelineShaderStageCreateInfo{}
		.setStage(vk::ShaderStageFlagBits::eFragment)
		.setModule(fragShaderModule.get())
		.setPName("main");
	std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages{ vertShaderStageInfo, fragShaderStageInfo };

	// Set up pipeline
	std::array<vk::DynamicState, 2> dynamicStates{ vk::DynamicState::eViewport, vk::DynamicState::eScissor };
	auto& dynStateInfo = vk::PipelineDynamicStateCreateInfo{}.setDynamicStates(dynamicStates);

	auto& vertexInputInfo = vk::PipelineVertexInputStateCreateInfo{}
		.setVertexBindingDescriptionCount(0)
		.setVertexAttributeDescriptionCount(0);
	auto& inputAssemblyInfo = vk::PipelineInputAssemblyStateCreateInfo{}
		.setTopology(vk::PrimitiveTopology::eTriangleList)
		.setPrimitiveRestartEnable(vk::False);

	auto& viewport = vk::Viewport{}
		.setX(0.0f).setY(0.0f)
		.setWidth(fbWidth).setHeight(fbHeight)
		.setMinDepth(0.0f).setMaxDepth(1.0f);
	std::array viewports = { viewport };

	auto& scissor = vk::Rect2D{}
		.setOffset({ 0, 0 })
		.setExtent(imgExtent);
	std::array scissors = { scissor };

	auto& viewportStateInfo = vk::PipelineViewportStateCreateInfo{}
		.setViewportCount(1)
		.setScissorCount(1);
	auto& rastStateInfo = vk::PipelineRasterizationStateCreateInfo{}
		.setDepthClampEnable(vk::False)
		.setRasterizerDiscardEnable(vk::False)
		.setPolygonMode(vk::PolygonMode::eFill)
		.setLineWidth(1.0f)
		.setCullMode(vk::CullModeFlagBits::eBack)
		.setFrontFace(vk::FrontFace::eClockwise)
		.setDepthBiasEnable(vk::False);
	auto& msStateInfo = vk::PipelineMultisampleStateCreateInfo{}
		.setSampleShadingEnable(vk::False)
		.setRasterizationSamples(vk::SampleCountFlagBits::e1);

	auto& colourBlendAttachmentState = vk::PipelineColorBlendAttachmentState{}
		.setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA)
		.setBlendEnable(vk::False);
	std::array colourBlendAttachmentStates{ colourBlendAttachmentState };
	auto& colourBlendStateInfo = vk::PipelineColorBlendStateCreateInfo{}
		.setLogicOpEnable(vk::False)
		.setAttachments(colourBlendAttachmentStates);

	auto& pipelineLayoutInfo = vk::PipelineLayoutCreateInfo{};
	auto pipelineLayout = device->createPipelineLayoutUnique(pipelineLayoutInfo);

	// Create render pass
	auto& colourAttachment = vk::AttachmentDescription{}
		.setFormat(srfFmt.format)
		.setSamples(vk::SampleCountFlagBits::e1)
		.setLoadOp(vk::AttachmentLoadOp::eClear)
		.setStoreOp(vk::AttachmentStoreOp::eStore)
		.setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
		.setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
		.setInitialLayout(vk::ImageLayout::eUndefined)
		.setFinalLayout(vk::ImageLayout::ePresentSrcKHR);
	std::array<vk::AttachmentDescription, 1> colourAttachments{ colourAttachment };

	auto& colourAttachmentRef = vk::AttachmentReference{}
		.setAttachment(0)
		.setLayout(vk::ImageLayout::eColorAttachmentOptimal);
	std::array<vk::AttachmentReference, 1> colourAttachmentRefs{ colourAttachmentRef };

	auto& subpass = vk::SubpassDescription{}
		.setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
		.setColorAttachments(colourAttachmentRefs);
	std::array<vk::SubpassDescription, 1> subpasses{ subpass };

	auto& dependency = vk::SubpassDependency{}
		.setSrcSubpass(vk::SubpassExternal)
		.setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput)
		.setSrcAccessMask({})
		.setDstSubpass(0)
		.setDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput)
		.setDstAccessMask(vk::AccessFlagBits::eColorAttachmentWrite);
	std::array dependencies = { dependency };

	auto& renderPassInfo = vk::RenderPassCreateInfo{}
		.setAttachments(colourAttachments)
		.setSubpasses(subpasses)
		.setDependencies(dependencies);
	auto renderPass = device->createRenderPassUnique(renderPassInfo);

	auto& pipelineInfo = vk::GraphicsPipelineCreateInfo{}
		.setStages(shaderStages)
		.setPVertexInputState(&vertexInputInfo)
		.setPInputAssemblyState(&inputAssemblyInfo)
		.setPViewportState(&viewportStateInfo)
		.setPRasterizationState(&rastStateInfo)
		.setPMultisampleState(&msStateInfo)
		.setPColorBlendState(&colourBlendStateInfo)
		.setPDynamicState(&dynStateInfo)
		.setLayout(pipelineLayout.get())
		.setRenderPass(renderPass.get())
		.setSubpass(0);
	auto graphicsPipelineRV = device->createGraphicsPipelineUnique({}, pipelineInfo);
	EXIT_ON_VULKAN_ERROR(graphicsPipelineRV.result);
	auto& graphicsPipeline = graphicsPipelineRV.value;


	std::vector<vk::UniqueFramebuffer> frameBuffers(FRAMES_IN_FLIGHT);
	for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
		std::array<vk::ImageView, 1> attachments{ swapchainImageViews[i].get() };
		auto& frameBufferInfo = vk::FramebufferCreateInfo{}
			.setRenderPass(renderPass.get())
			.setAttachments(attachments)
			.setWidth(fbWidth).setHeight(fbHeight)
			.setLayers(1u);
		frameBuffers[i] = device->createFramebufferUnique(frameBufferInfo);
	}
	auto& commandPoolInfo = vk::CommandPoolCreateInfo{}
		.setQueueFamilyIndex(selectedQueueFamilyIndex)
		.setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
	auto commandPool = device->createCommandPoolUnique(commandPoolInfo);

	auto& comBufAllocInfo = vk::CommandBufferAllocateInfo{}
		.setCommandPool(commandPool.get())
		.setLevel(vk::CommandBufferLevel::ePrimary)
		.setCommandBufferCount(FRAMES_IN_FLIGHT);
	auto& cmdBuffers = device->allocateCommandBuffersUnique(comBufAllocInfo);

	auto& clearValue = vk::ClearValue{}.setColor({ 0.0f, 0.0f, 0.0f, 1.0f });
	std::array clearValues{ clearValue };
	for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
		auto& img = swapchainImageViews[i];

		cmdBuffers[i]->begin(vk::CommandBufferBeginInfo{});

		auto& renderPassBeginInfo = vk::RenderPassBeginInfo{}
			.setRenderPass(renderPass.get())
			.setFramebuffer(frameBuffers[i].get())
			.setRenderArea(vk::Rect2D{}.setOffset({ 0, 0 }).setExtent(imgExtent))
			.setClearValues(clearValues);

		cmdBuffers[i]->beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
		cmdBuffers[i]->bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline.get());

		cmdBuffers[i]->setViewport(0u, viewports);
		cmdBuffers[i]->setScissor(0u, scissors);

		cmdBuffers[i]->draw(3u, 1u, 0u, 0u);

		cmdBuffers[i]->endRenderPass();
		cmdBuffers[i]->end();
	}

	// Create reusable fences and semaphores
	std::array<vk::UniqueSemaphore, FRAMES_IN_FLIGHT> imageAcquiredSemaphores;
	std::generate(imageAcquiredSemaphores.begin(), imageAcquiredSemaphores.end(),
		[d = device.get()]() {
			return d.createSemaphoreUnique(vk::SemaphoreCreateInfo{});
		});
	std::array<vk::UniqueSemaphore, FRAMES_IN_FLIGHT> renderFinishedSemaphores;
	std::generate(renderFinishedSemaphores.begin(), renderFinishedSemaphores.end(),
		[d = device.get()]() {
			return d.createSemaphoreUnique(vk::SemaphoreCreateInfo{});
		});
	std::array<vk::UniqueFence, FRAMES_IN_FLIGHT> frameFinishedFences;
	std::generate(frameFinishedFences.begin(), frameFinishedFences.end(),
		[d = device.get()]() {
			return d.createFenceUnique(vk::FenceCreateInfo{}.setFlags(vk::FenceCreateFlagBits::eSignaled));
		});

	uint32_t frameIdx = -1u;
	while (!glfwWindowShouldClose(window)) {
		++frameIdx %= FRAMES_IN_FLIGHT;

		std::array fences = { frameFinishedFences[frameIdx].get() };
		CHECK_VULKAN_RESULT(device->waitForFences(fences, vk::True, std::numeric_limits<uint64_t>::max()));
		device->resetFences(fences);

		auto imageIndexRV = device->acquireNextImageKHR(swapchain.get(), std::numeric_limits<uint32_t>::max(), imageAcquiredSemaphores[frameIdx].get(), nullptr);
		EXIT_ON_VULKAN_ERROR(imageIndexRV.result);
		uint32_t imageIndex = imageIndexRV.value;
		std::array imageIndices = { imageIndex };

		std::array submitCmdBuffers = { cmdBuffers[frameIdx].get() };
		std::array submitWaitSemaphores = { imageAcquiredSemaphores[frameIdx].get() };
		std::array submitWaitStages = { vk::PipelineStageFlags{vk::PipelineStageFlagBits::eColorAttachmentOutput} };
		std::array submitSignalSemaphores = { renderFinishedSemaphores[frameIdx].get() };

		auto submitInfo = vk::SubmitInfo{}
			.setCommandBuffers(submitCmdBuffers)
			.setWaitSemaphores(submitWaitSemaphores)
			.setSignalSemaphores(submitSignalSemaphores)
			.setWaitDstStageMask(submitWaitStages);
		queue.submit(submitInfo, frameFinishedFences[frameIdx].get());

		auto& presentInfo = vk::PresentInfoKHR{}
			.setImageIndices(imageIndices)
			.setWaitSemaphores(submitSignalSemaphores)
			.setSwapchains(swapchains);

		EXIT_ON_VULKAN_ERROR(queue.presentKHR(presentInfo));

		glfwPollEvents();
		if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
			glfwSetWindowShouldClose(window, GLFW_TRUE);
	}
	device->waitIdle();
	glfwTerminate();
}