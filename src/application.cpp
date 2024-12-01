#include <application.h>
#include <logging.h>
#include <tuple>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace vkrt {

auto bufferDeviceAddressFeatures = vk::PhysicalDeviceBufferDeviceAddressFeatures{}
.setBufferDeviceAddress(vk::True)
#ifndef NDEBUG
.setBufferDeviceAddressCaptureReplay(vk::True)
#endif
;

Application::Application(std::string appName, uint32_t width, uint32_t height, const uint32_t apiVersion,
						 vk::ArrayProxyNoTemporaries<const char* const> const& appendInstanceExtensions,
						 vk::ArrayProxyNoTemporaries<const char* const> const& appendLayers,
						 vk::ArrayProxyNoTemporaries<const char* const> const& appendDeviceExtensions,
						 const void* additionalFeaturesChain,
						 bool preferDedicatedGPU, bool separateTransferQueue, bool separateComputeQueue,
						 uint32_t framesInFlight, vk::ImageUsageFlags swapchainImUsage,
						 vk::ArrayProxy<vk::SurfaceFormatKHR> const& preferredFormats,
						 vk::ArrayProxy<vk::PresentModeKHR> const& preferredPresModes)
	: appName(appName)
	, width(width)
	, height(height)
	, apiVersion(apiVersion)
	, framesInFlight(framesInFlight)
	, swapchainImUsage(swapchainImUsage)
{
	VULKAN_HPP_DEFAULT_DISPATCHER.init();

	// Get required GLFW extensions
	glfwInit();
	std::vector<const char*> glfwExtensions;
	uint32_t glfwReqExtCount;
	auto glfwReqExt = glfwGetRequiredInstanceExtensions(&glfwReqExtCount);
	for (int i = 0; i < glfwReqExtCount; i++) glfwExtensions.push_back(glfwReqExt[i]);

	// Create instance
	for (const auto& ext : glfwExtensions)
		instanceExtensions.insert(ext);
	for (const auto& ext : appendInstanceExtensions)
		instanceExtensions.insert(ext);
	for (const auto& ext : appendLayers)
		validationLayers.insert(ext);

	featuresChain.setPNext(&bufferDeviceAddressFeatures);
	if (additionalFeaturesChain) {
		vk::PhysicalDeviceFeatures2* feature = (vk::PhysicalDeviceFeatures2*)featuresChain.pNext; // cast to access pNext field in features
		while (feature->pNext) feature = (vk::PhysicalDeviceFeatures2*)feature->pNext;
		feature->setPNext((void*)additionalFeaturesChain); // sketchy casting, but won't be changing the value
	}

	createInstance();
	createWindow();
	createSurface();

	// Append required device extensions
	for (const auto& ext : appendDeviceExtensions)
		deviceExtensions.insert(ext);
	selectPhysicalDevice(preferDedicatedGPU);
	createDevice(separateTransferQueue, separateComputeQueue);

	dmm = std::make_unique<DeviceMemoryManager>(device, physicalDevice);
	rch = std::make_unique<ResourceCopyHandler>(device, transferQueue ? *transferQueue : graphicsQueue);

	determineSwapchainSettings(preferredFormats, preferredPresModes);
	createSwapchain();
}

Application::~Application() {
	glfwDestroyWindow(window);
}

void Application::createInstance() {
	std::vector<const char*> instanceExtensionsData, validationLayersData;
	std::copy(instanceExtensions.begin(), instanceExtensions.end(), std::back_inserter(instanceExtensionsData));
	std::copy(validationLayers.begin(), validationLayers.end(), std::back_inserter(validationLayersData));
	auto& appInfo = vk::ApplicationInfo{}
		.setPApplicationName(appName.c_str())
		.setApiVersion(apiVersion)
		.setApplicationVersion(0)
		.setPEngineName("vkrt");
	auto& instanceCI = vk::InstanceCreateInfo{}
		.setPApplicationInfo(&appInfo)
		.setPEnabledExtensionNames(instanceExtensionsData)
		.setPEnabledLayerNames(validationLayersData);

	instance = vk::createInstanceUnique(instanceCI);
	VULKAN_HPP_DEFAULT_DISPATCHER.init(*instance);
}

void Application::createWindow() {
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	window = glfwCreateWindow(width, height, appName.c_str(), nullptr, nullptr);
	glfwSetWindowUserPointer(window, this);
	glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
}

void Application::createSurface() {
	VkSurfaceKHR surfaceTmp{};
	CHECK_VULKAN_RESULT(glfwCreateWindowSurface(*instance, window, nullptr, &surfaceTmp));
	surface = vk::UniqueSurfaceKHR(surfaceTmp, *instance);
}

void Application::selectPhysicalDevice(bool preferDedicatedGPU) {
	auto& physicalDevices = instance->enumeratePhysicalDevices();
	std::vector<vk::PhysicalDevice> eligibleDevices;
	eligibleDevices.reserve(physicalDevices.size());
	auto& queueFlags = vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute;

	for (const auto& pd : physicalDevices) {
		// Get device extensions
		const auto& availableDevExtensionProps = pd.enumerateDeviceExtensionProperties();
		std::vector<const char*> availableDevExtensionNames(availableDevExtensionProps.size());
		std::transform(availableDevExtensionProps.begin(), availableDevExtensionProps.end(), availableDevExtensionNames.begin(),
					   [](const vk::ExtensionProperties& ep) {
						   return ep.extensionName.data();
					   });
		std::sort(availableDevExtensionNames.begin(), availableDevExtensionNames.end(), cstrless());
		// Are required extensions supported?
		bool extsSupported = std::includes(availableDevExtensionNames.begin(), availableDevExtensionNames.end(),
										   deviceExtensions.begin(), deviceExtensions.end(),
										   cstrless());
		if (!extsSupported) continue;

		bool presSupported = false;
		vk::QueueFlags supportedQueues;
		uint32_t currQueueFamilyIndex = 0u;
		for (const auto& qfp : pd.getQueueFamilyProperties()) {
			supportedQueues |= qfp.queueFlags;
			presSupported = presSupported || pd.getSurfaceSupportKHR(currQueueFamilyIndex, *surface);

			// Are all required queues supported?
			bool queuesSupported = (queueFlags | supportedQueues) == supportedQueues;
			if (queuesSupported && presSupported) {
				eligibleDevices.push_back(pd);
				break;
			}
			currQueueFamilyIndex++;
		}
	}
	if (eligibleDevices.size() == 0) {
		LOG_ERROR("Could not find physical device with required properties");
		throw std::runtime_error("Could not find physical device with required properties");
	}

	// Pick best device according to preferences
	std::sort(eligibleDevices.begin(), eligibleDevices.end(),
			  [preferDedicatedGPU](const vk::PhysicalDevice& a, const vk::PhysicalDevice& b) {
				  auto& propsA = a.getProperties();
				  auto& propsB = b.getProperties();
				  // Prioritise device type first
				  if (preferDedicatedGPU) {
					  if (propsA.deviceType == vk::PhysicalDeviceType::eDiscreteGpu && propsB.deviceType != vk::PhysicalDeviceType::eDiscreteGpu)
						  return true;
					  else if (propsA.deviceType != vk::PhysicalDeviceType::eDiscreteGpu && propsB.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
						  return false;
				  }

				  // Prioritise by more memory
				  auto& memHeapsA = a.getMemoryProperties().memoryHeaps;
				  auto& memHeapsB = b.getMemoryProperties().memoryHeaps;
				  int memA, memB;

				  for (auto& heap : memHeapsA) {
					  if (heap.flags & vk::MemoryHeapFlagBits::eDeviceLocal) memA = heap.size;
				  }
				  for (auto& heap : memHeapsB) {
					  if (heap.flags & vk::MemoryHeapFlagBits::eDeviceLocal) memB = heap.size;
				  }

				  return memA < memB;
			  });
	physicalDevice = eligibleDevices.front();
#ifndef NDEBUG
	LOG_INFO("Selected device: %s", physicalDevice.getProperties().deviceName.data());
#endif
}

std::array<uint32_t, 3> Application::selectQueues(bool separateTransferQueue, bool separateComputeQueue) {
	auto& qfps = physicalDevice.getQueueFamilyProperties();
	std::array selectedQueues{ -1u, -1u, -1u };

	// Search for graphics queue
	uint32_t currQueueIndex = 0u;
	for (const auto& qfp : qfps) {
		if ((qfp.queueFlags & (vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer)) &&
			physicalDevice.getSurfaceSupportKHR(currQueueIndex, *surface)) {
			selectedQueues[0] = currQueueIndex;
		}
		if (separateTransferQueue) {
			if ((qfp.queueFlags & vk::QueueFlagBits::eTransfer) &&
				!(qfp.queueFlags & (vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute))) {
				selectedQueues[1] = currQueueIndex;
			}
		}
		if (separateComputeQueue) {
			if ((qfp.queueFlags & vk::QueueFlagBits::eCompute) &&
				!(qfp.queueFlags & vk::QueueFlagBits::eGraphics)) {
				selectedQueues[2] = currQueueIndex;
			}
		}
		currQueueIndex++;
	}

	return selectedQueues;
}

void Application::createDevice(bool separateTransferQueue, bool separateComputeQueue) {
	const auto& queueFamilyIndices = selectQueues(separateTransferQueue, separateComputeQueue);
	std::vector<vk::DeviceQueueCreateInfo> queueCIs;
	std::array queuePriorities = { 1.0f };
	queueCIs.reserve(queueFamilyIndices.size());

	for (const auto& qfi : queueFamilyIndices) {
		if (qfi != -1u)
			queueCIs.push_back(vk::DeviceQueueCreateInfo{}
							   .setQueueFamilyIndex(qfi)
							   .setQueuePriorities(queuePriorities));
	}

	std::vector<const char*> deviceExtensionsData;
	std::copy(deviceExtensions.begin(), deviceExtensions.end(), std::back_inserter(deviceExtensionsData));
	auto& deviceCI = vk::DeviceCreateInfo{}
		.setPNext(&featuresChain)
		.setPEnabledExtensionNames(deviceExtensionsData)
		.setQueueCreateInfos(queueCIs);
	vk::Device deviceTmp = physicalDevice.createDevice(deviceCI);
	device = vk::SharedHandle(deviceTmp);

	graphicsQueue = { queueFamilyIndices[0], device->getQueue(queueFamilyIndices[0], 0) };
	if (separateTransferQueue) transferQueue = { queueFamilyIndices[1], device->getQueue(queueFamilyIndices[1], 0) };
	if (separateComputeQueue) computeQueue = { queueFamilyIndices[2], device->getQueue(queueFamilyIndices[2], 0) };
	VULKAN_HPP_DEFAULT_DISPATCHER.init(*device);
}

void Application::createSwapchain() {
	auto& swapchainCI = vk::SwapchainCreateInfoKHR{}
		.setSurface(*surface)
		.setImageExtent(vk::Extent2D{ width,height })
		.setMinImageCount(framesInFlight)
		.setImageArrayLayers(1u)
		.setImageUsage(swapchainImUsage)
		.setImageFormat(swapchainFormat.format)
		.setImageColorSpace(swapchainFormat.colorSpace)
		.setPresentMode(presentMode);

	swapchain.reset();
	swapchain = device->createSwapchainKHRUnique(swapchainCI);

	swapchainImages = device->getSwapchainImagesKHR(*swapchain);
}

void Application::determineSwapchainSettings(const vk::ArrayProxy<vk::SurfaceFormatKHR>& preferredFormats,
											 const vk::ArrayProxy<vk::PresentModeKHR>& preferredPresModes) {
	auto& availableFormats = physicalDevice.getSurfaceFormatsKHR(*surface);
	auto& availablePresModes = physicalDevice.getSurfacePresentModesKHR(*surface);

	swapchainFormat = availableFormats[0];
	presentMode = availablePresModes[0];

	// Find format
	for (const auto& pf : preferredFormats) {
		if (auto& it = std::find_if(availableFormats.begin(), availableFormats.end(),
									[pf](vk::SurfaceFormatKHR format) {
										return format.format == pf.format && format.colorSpace == pf.colorSpace;
									}); it != availableFormats.end()) {
			swapchainFormat = *it;
			break;
		};
	}

	// Find presentation mode
	for (const auto& ppm : preferredPresModes) {
		if (auto& it = std::find(availablePresModes.begin(), availablePresModes.end(), ppm); it != availablePresModes.end()) {
			presentMode = *it;
			break;
		};
	}


}

void Application::recreateSwapchain() {
	glfwGetWindowSize(window, reinterpret_cast<int*>(&width), reinterpret_cast<int*>(&height));
	if (width == 0 || height == 0)
		minimised = true;
	else
		minimised = false;

	device->waitIdle();
	//oldSwapchain = std::move(swapchain);
	createSwapchain();
}

void Application::handleResize() {
	framebufferResized = false;
	recreateSwapchain();
}

void Application::framebufferResizeCallback(GLFWwindow* window, int width, int height) {
	auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
	app->framebufferResized = true;
}

void Application::renderLoop() {
	// Create reusable fences and semaphores
	std::vector<vk::SharedSemaphore> imageAcquiredSemaphores(framesInFlight);
	std::generate(imageAcquiredSemaphores.begin(), imageAcquiredSemaphores.end(),
				  [this]() {
					  return vk::SharedSemaphore(device->createSemaphore({}), device);
				  });
	std::vector<vk::SharedSemaphore> renderFinishedSemaphores(framesInFlight);
	std::generate(renderFinishedSemaphores.begin(), renderFinishedSemaphores.end(),
				  [this]() {
					  return vk::SharedSemaphore(device->createSemaphore({}), device);
				  });
	std::vector<vk::SharedFence> frameFinishedFences(framesInFlight);
	std::generate(frameFinishedFences.begin(), frameFinishedFences.end(),
				  [this]() {
					  return vk::SharedFence(device->createFence(vk::FenceCreateInfo{}.setFlags(vk::FenceCreateFlagBits::eSignaled)), device);
				  });

	// Render loop
	uint32_t frameIdx = 0u;
	while (!glfwWindowShouldClose(window)) {
		CHECK_VULKAN_RESULT(device->waitForFences(*frameFinishedFences[frameIdx], vk::True, std::numeric_limits<uint64_t>::max()));

		uint32_t imageIndex;
		try {
			auto imageIndexRV = device->acquireNextImageKHR(*swapchain, std::numeric_limits<uint32_t>::max(), *imageAcquiredSemaphores[frameIdx], nullptr);
			imageIndex = imageIndexRV.value;
		} catch (vk::OutOfDateKHRError const& e) {
			handleResize();
			continue;
		}

		device->resetFences(*frameFinishedFences[frameIdx]);
		drawFrame(frameIdx, imageAcquiredSemaphores[frameIdx], renderFinishedSemaphores[frameIdx], frameFinishedFences[frameIdx]);

		const vk::Semaphore& renderFinishedSemaphore = *(renderFinishedSemaphores[frameIdx]);
		auto& presentInfo = vk::PresentInfoKHR{}
			.setWaitSemaphores(renderFinishedSemaphore)
			.setSwapchains(*swapchain)
			.setPImageIndices(&imageIndex);
		try {
			const auto& res = std::get<vk::Queue>(graphicsQueue).presentKHR(presentInfo);
			if (res == vk::Result::eSuboptimalKHR || framebufferResized)
				handleResize();
		} catch (vk::OutOfDateKHRError const& e) {
			handleResize();
		}

		glfwPollEvents();
		if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
			glfwSetWindowShouldClose(window, GLFW_TRUE);

		++frameIdx %= framesInFlight;
	}

	device->waitIdle();
	glfwTerminate();

}

}