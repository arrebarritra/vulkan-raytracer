#pragma once

#include <vulkan/vulkan.hpp>
#include <GLFW/glfw3.h>

#include <optional>

namespace vkrt {

class Application {

public:
	void renderLoop();

protected:
	Application(std::string appName, uint32_t width = 800, uint32_t height = 600, const uint32_t apiVersion = vk::ApiVersion12,
				vk::ArrayProxyNoTemporaries<const char* const> const& appendInstanceExtensions = nullptr,
				vk::ArrayProxyNoTemporaries<const char* const> const& appendLayers = nullptr,
				vk::ArrayProxyNoTemporaries<const char* const> const& appendDeviceExtensions = nullptr,
				bool preferDedicatedGPU = true, bool separateTransferQueue = false, bool separateComputeQueue = false,
				uint32_t framesInFlight = 3u, vk::ImageUsageFlags swapchainImUsage = vk::ImageUsageFlagBits::eColorAttachment,
				vk::ArrayProxy<vk::SurfaceFormatKHR> const& preferredFormats = nullptr,
				vk::ArrayProxy<vk::PresentModeKHR> const& preferredPresModes = nullptr);

	~Application() ;

	std::string appName;

	// Instance info
	vk::UniqueInstance instance;
	uint32_t apiVersion;
	std::vector<const char*> instanceExtensions;
	std::vector<const char*> validationLayers{
#ifndef NDEBUG
		"VK_LAYER_KHRONOS_validation"
#endif
	};

	// Window
	uint32_t width, height;
	GLFWwindow* window;
	vk::UniqueSurfaceKHR surface;
	bool framebufferResized = false;
	bool minimised = true;

	// Devices
	vk::PhysicalDevice physicalDevice;
	vk::UniqueDevice device;

	// Swapchains
	uint32_t framesInFlight;
	vk::ImageUsageFlags swapchainImUsage;
	vk::SurfaceFormatKHR swapchainFormat;
	vk::PresentModeKHR presentMode;

	vk::UniqueSwapchainKHR swapchain;
    //vk::UniqueSwapchainKHR oldSwapchain;
	std::vector<vk::Image> swapchainImages;

	// Queues
	vk::Queue graphicsQueue;
	std::optional<vk::Queue> computeQueue, transferQueue;

	virtual void handleResize();
	virtual void drawFrame(uint32_t frameIdx, vk::Semaphore imageAcquiredSemaphore, vk::Semaphore renderFinishedSemaphore,
						vk::Fence frameFinishedFence) = 0;

private:
	// Device selection parameters
	std::vector<const char*> deviceExtensions{ vk::KHRSwapchainExtensionName };

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
};

}