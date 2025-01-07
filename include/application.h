#pragma once

#include <vulkan_headers.h>
#include <GLFW/glfw3.h>

#include <optional>
#include <set>

#include <devicememorymanager.h>
#include <buffer.h>
#include <image.h>
#include <resourcetransferhandler.h>
#include <camera.h>


namespace vkrt {

class Application {

public:
	void renderLoop();

protected:
	Application(std::string appName, uint32_t width = 800, uint32_t height = 600, const uint32_t apiVersion = vk::ApiVersion12,
				vk::ArrayProxyNoTemporaries<const char* const> const& appendInstanceExtensions = nullptr,
				vk::ArrayProxyNoTemporaries<const char* const> const& appendLayers = nullptr,
				vk::ArrayProxyNoTemporaries<const char* const> const& appendDeviceExtensions = nullptr,
				const void* additionalFeaturesChain = nullptr,
				bool preferDedicatedGPU = true, bool separateTransferQueue = false, bool separateComputeQueue = false,
				uint32_t framesInFlight = 3u, vk::ImageUsageFlags swapchainImUsage = vk::ImageUsageFlagBits::eColorAttachment,
				vk::ArrayProxy<vk::SurfaceFormatKHR> const& preferredFormats = nullptr,
				vk::ArrayProxy<vk::PresentModeKHR> const& preferredPresModes = nullptr);

	~Application();

	std::string appName;

	// Instance info
	vk::UniqueInstance instance;
	uint32_t apiVersion;

	// Window
	uint32_t width, height;
	GLFWwindow* window;
	vk::UniqueSurfaceKHR surface;
	bool framebufferResized = false;
	bool minimised = true;

	// Devices
	vk::PhysicalDevice physicalDevice;
	vk::SharedDevice device;

	// Memory and resource management
	std::unique_ptr<DeviceMemoryManager> dmm;
	std::unique_ptr<ResourceTransferHandler> rth;

	// Swapchains
	uint32_t framesInFlight;
	vk::ImageUsageFlags swapchainImUsage;
	vk::SurfaceFormatKHR swapchainFormat;
	vk::PresentModeKHR presentMode;

	vk::UniqueSwapchainKHR swapchain;
	//vk::UniqueSwapchainKHR oldSwapchain;
	std::vector<vk::Image> swapchainImages;

	// Queues
	std::tuple<uint32_t, vk::Queue> graphicsQueue;
	std::optional<std::tuple<uint32_t, vk::Queue>> computeQueue, transferQueue;

	// Input
	Camera camera;
	double lastXPos, lastYPos;
	double frameTime = 0.0;
	bool firstFrame = true;

	virtual void handleResize();
	virtual void createCommandPools() = 0;
	virtual void drawFrame(uint32_t imageIdx, uint32_t frameIdx, vk::SharedSemaphore imageAcquiredSemaphore, vk::SharedSemaphore renderFinishedSemaphore,
						   vk::SharedFence frameFinishedFence) = 0;

private:
	struct cstrless {
		bool operator()(const char* a, const char* b) const {
			return strcmp(a, b) < 0;
		}
	};

	// Extensions and features
	std::set<const char*, cstrless> instanceExtensions;
	std::set<const char*, cstrless> layers{
#ifndef NDEBUG
		"VK_LAYER_KHRONOS_validation"
#endif
	};
	std::set<const char*, cstrless> deviceExtensions{
		vk::KHRSwapchainExtensionName,
		vk::KHRBufferDeviceAddressExtensionName,
		vk::KHRShaderNonSemanticInfoExtensionName,
		vk::EXTScalarBlockLayoutExtensionName
	};
#ifndef NDEBUG
	std::vector<vk::ValidationFeatureEnableEXT> enabledValidationFeatures = { vk::ValidationFeatureEnableEXT::eDebugPrintf };
#endif
	vk::PhysicalDeviceFeatures2 featuresChain;

	void createInstance();
	void createWindow();
	void createSurface();
	void selectPhysicalDevice(bool preferDedicatedGPU);
	std::array<uint32_t, 3> selectQueues(bool separateTransferQueue, bool separateComputeQueue);
	void createDevice(bool separateTransferQueue, bool separateComputeQueue);
	void createSwapchain();
	void recreateSwapchain();
	void determineSwapchainSettings(vk::ArrayProxy<vk::SurfaceFormatKHR> const& preferredFormats,
									vk::ArrayProxy<vk::PresentModeKHR> const& preferredPresModes);

	static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
	static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
	static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos);
};

}