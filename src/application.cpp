#include <application.h>
#include <logging.h>
#include <tuple>
#include <chrono>
#include <utils.h>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace vkrt {

auto scalarBlockLayoutFeatures = vk::PhysicalDeviceScalarBlockLayoutFeaturesEXT{}
.setScalarBlockLayout(vk::True);
auto bufferDeviceAddressFeatures = vk::PhysicalDeviceBufferDeviceAddressFeatures{}
.setBufferDeviceAddress(vk::True)
#ifndef NDEBUG
.setBufferDeviceAddressCaptureReplay(vk::True)
#endif
.setPNext(&scalarBlockLayoutFeatures)
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
	camera.aspect = width / static_cast<float>(height);
	uint32_t glfwReqExtCount;
	auto glfwReqExt = glfwGetRequiredInstanceExtensions(&glfwReqExtCount);
	std::vector<const char*> glfwExtensions(glfwReqExtCount);
	for (int i = 0; i < glfwReqExtCount; i++) glfwExtensions[i] = glfwReqExt[i];

	// Create instance
	for (const auto& ext : glfwExtensions)
		instanceExtensions.insert(ext);
	for (const auto& ext : appendInstanceExtensions)
		instanceExtensions.insert(ext);
	for (const auto& ext : appendLayers)
		layers.insert(ext);

	featuresChain.features.setShaderInt64(vk::True);
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
	rth = std::make_unique<ResourceTransferHandler>(device, transferQueue ? *transferQueue : graphicsQueue);

	determineSwapchainSettings(preferredFormats, preferredPresModes);
	createSwapchain();
}

Application::~Application() {
	glfwDestroyWindow(window);
}

void Application::createInstance() {
	std::vector<const char*> instanceExtensionsData, validationLayersData;
	std::copy(instanceExtensions.begin(), instanceExtensions.end(), std::back_inserter(instanceExtensionsData));
	std::copy(layers.begin(), layers.end(), std::back_inserter(validationLayersData));


	auto appInfo = vk::ApplicationInfo{}
		.setPApplicationName(appName.c_str())
		.setApiVersion(apiVersion)
		.setApplicationVersion(0)
		.setPEngineName("vkrt");
	auto instanceCI = vk::InstanceCreateInfo{}
		.setPApplicationInfo(&appInfo)
		.setPEnabledExtensionNames(instanceExtensionsData)
		.setPEnabledLayerNames(validationLayersData);

#ifndef NDEBUG
	auto validationFeatures = vk::ValidationFeaturesEXT{}.setEnabledValidationFeatures(enabledValidationFeatures);
	instanceCI.setPNext(&validationFeatures);
#endif

	instance = vk::createInstanceUnique(instanceCI);
	VULKAN_HPP_DEFAULT_DISPATCHER.init(*instance);
}

void Application::createWindow() {
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	window = glfwCreateWindow(width, height, appName.c_str(), nullptr, nullptr);
	glfwHideWindow(window);
	glfwSetWindowUserPointer(window, this);
	glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
	glfwSetWindowIconifyCallback(window, iconifyCallback);
	glfwSetKeyCallback(window, keyCallback);
	glfwSetCursorPosCallback(window, cursorPosCallback);
}

void Application::createSurface() {
	VkSurfaceKHR surfaceTmp{};
	CHECK_VULKAN_RESULT(glfwCreateWindowSurface(*instance, window, nullptr, &surfaceTmp));
	surface = vk::UniqueSurfaceKHR(surfaceTmp, *instance);
}

void Application::selectPhysicalDevice(bool preferDedicatedGPU) {
	auto physicalDevices = instance->enumeratePhysicalDevices();
	std::vector<vk::PhysicalDevice> eligibleDevices;
	eligibleDevices.reserve(physicalDevices.size());
	auto queueFlags = vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer; // Main queue

	for (const auto& pd : physicalDevices) {
		// Get device extensions
		const auto availableDevExtensionProps = pd.enumerateDeviceExtensionProperties();
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
				  auto propsA = a.getProperties();
				  auto propsB = b.getProperties();
				  // Prioritise device type first
				  if (preferDedicatedGPU) {
					  if (propsA.deviceType == vk::PhysicalDeviceType::eDiscreteGpu && propsB.deviceType != vk::PhysicalDeviceType::eDiscreteGpu)
						  return true;
					  else if (propsA.deviceType != vk::PhysicalDeviceType::eDiscreteGpu && propsB.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
						  return false;
				  }

				  // Prioritise by more memory
				  auto memHeapsA = a.getMemoryProperties().memoryHeaps;
				  auto memHeapsB = b.getMemoryProperties().memoryHeaps;
				  int memA, memB;

				  for (const auto& heap : memHeapsA) {
					  if (heap.flags & vk::MemoryHeapFlagBits::eDeviceLocal) memA = heap.size;
				  }
				  for (const auto& heap : memHeapsB) {
					  if (heap.flags & vk::MemoryHeapFlagBits::eDeviceLocal) memB = heap.size;
				  }

				  return memA < memB;
			  });
	physicalDevice = eligibleDevices.front();
	LOG_INFO("Selected device: %s", physicalDevice.getProperties().deviceName.data());
}

std::array<uint32_t, 3> Application::selectQueues(bool separateTransferQueue, bool separateComputeQueue) {
	auto qfps = physicalDevice.getQueueFamilyProperties();
	std::array selectedQueues{ -1u, -1u, -1u };

	// Search for graphics queue
	uint32_t currQueueIndex = -1u;
	for (const auto& qfp : qfps) {
		currQueueIndex++;
		if (selectedQueues[0] == -1u && utils::isSubset(vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer, qfp.queueFlags) &&
			physicalDevice.getSurfaceSupportKHR(currQueueIndex, *surface)) {
			selectedQueues[0] = currQueueIndex;
			continue;
		}
		if (separateTransferQueue && selectedQueues[1] == -1u) {
			if (utils::isSubset(vk::QueueFlags{ vk::QueueFlagBits::eTransfer }, qfp.queueFlags)) {
				selectedQueues[1] = currQueueIndex;
				continue;
			}
		}
		if (separateComputeQueue && selectedQueues[2] == -1u) {
			if (utils::isSubset(vk::QueueFlags{ vk::QueueFlagBits::eCompute }, qfp.queueFlags)) {
				selectedQueues[2] = currQueueIndex;
				continue;
			}
		}
	}

	return selectedQueues;
}

void Application::createDevice(bool separateTransferQueue, bool separateComputeQueue) {
	const auto queueFamilyIndices = selectQueues(separateTransferQueue, separateComputeQueue);
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
	auto deviceCI = vk::DeviceCreateInfo{}
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
	auto swapchainCI = vk::SwapchainCreateInfoKHR{}
		.setSurface(*surface)
		.setImageExtent(vk::Extent2D{ width,height })
		.setMinImageCount(std::max(physicalDevice.getSurfaceCapabilitiesKHR(*surface).minImageCount, framesInFlight))
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
	auto availableFormats = physicalDevice.getSurfaceFormatsKHR(*surface);
	auto availablePresModes = physicalDevice.getSurfacePresentModesKHR(*surface);

	swapchainFormat = availableFormats[0];
	presentMode = availablePresModes[0];

	// Find format
	for (const auto& pf : preferredFormats) {
		if (const auto& it = std::find_if(availableFormats.begin(), availableFormats.end(),
										  [pf](vk::SurfaceFormatKHR format) {
											  return format.format == pf.format && format.colorSpace == pf.colorSpace;
										  }); it != availableFormats.end()) {
			swapchainFormat = *it;
			break;
		};
	}

	// Find presentation mode
	for (const auto& ppm : preferredPresModes) {
		if (const auto& it = std::find(availablePresModes.begin(), availablePresModes.end(), ppm); it != availablePresModes.end()) {
			presentMode = *it;
			break;
		};
	}
}

void Application::recreateSwapchain() {
	device->waitIdle();
	//oldSwapchain = std::move(swapchain);
	createSwapchain();
}

void Application::handleResize() {
	glfwGetWindowSize(window, reinterpret_cast<int*>(&width), reinterpret_cast<int*>(&height));
	if (width == 0 || height == 0) {
		minimised = true;
		return;
	} else {
		minimised = false;
	}

	framebufferResized = false;
	camera.aspect = width / static_cast<float>(height);
	recreateSwapchain();
}

void Application::framebufferResizeCallback(GLFWwindow* window, int width, int height) {
	auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
	app->framebufferResized = true;
}

void Application::iconifyCallback(GLFWwindow* window, int iconified) {
	auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
	app->minimised = static_cast<bool>(iconified);
}

void Application::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
	auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
		glfwSetWindowShouldClose(window, GLFW_TRUE);
}

void Application::cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
	auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
	if (!app->firstFrame)
		app->camera.cursorPosCallback(window, xpos - app->lastXPos, ypos - app->lastYPos);
	app->lastXPos = xpos;
	app->lastYPos = ypos;
	if (app->firstFrame) app->firstFrame = false;
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
		auto frameStart = std::chrono::steady_clock::now();
		camera.processKeyInput(window, frameTime);
		//CHECK_VULKAN_RESULT(device->waitForFences(*frameFinishedFences[frameIdx], vk::True, std::numeric_limits<uint64_t>::max()));
		rth->flushPendingTransfers(frameFinishedFences[frameIdx]);

		uint32_t imageIdx = -1u;
		if (!minimised) {
			try {
				auto imageIdxRV = device->acquireNextImageKHR(*swapchain, std::numeric_limits<uint32_t>::max(), *imageAcquiredSemaphores[frameIdx], nullptr);
				imageIdx = imageIdxRV.value;
			} catch (vk::OutOfDateKHRError const& e) {
				handleResize();
				continue;
			}
		}

		device->resetFences(*frameFinishedFences[frameIdx]);
		drawFrame(imageIdx, frameIdx, imageAcquiredSemaphores[frameIdx], renderFinishedSemaphores[frameIdx], frameFinishedFences[frameIdx]);

		if (!minimised) {
			const vk::Semaphore& renderFinishedSemaphore = *(renderFinishedSemaphores[frameIdx]);
			auto presentInfo = vk::PresentInfoKHR{}
				.setWaitSemaphores(renderFinishedSemaphore)
				.setSwapchains(*swapchain)
				.setImageIndices(imageIdx);
			try {
				const auto res = std::get<vk::Queue>(graphicsQueue).presentKHR(presentInfo);
				if (framebufferResized || res == vk::Result::eSuboptimalKHR)
					handleResize();
			} catch (vk::OutOfDateKHRError const& e) {
				handleResize();
			}
		}

		glfwPollEvents();
		frameTime = (std::chrono::steady_clock::now() - frameStart).count() / 1e9;
		++frameIdx %= framesInFlight;
	}

	device->waitIdle();
	glfwTerminate();
}

}