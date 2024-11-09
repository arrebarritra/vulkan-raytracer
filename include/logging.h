#pragma once

#include <vulkan/vk_enum_string_helper.h>

#define __FILENAME__ (__FILE__ + SOURCE_PATH_SIZE) \

#define CHECK_VULKAN_RESULT(res) \
if (vk::Result(res) < vk::Result::eSuccess) { \
	fprintf(stderr, "%s(%d): %s (code %d)\n", __FILENAME__, __LINE__, string_VkResult(static_cast<VkResult>(res)), res); \
}

#define EXIT_ON_VULKAN_ERROR(res) \
if (vk::Result(res) < vk::Result::eSuccess) { \
	fprintf(stderr, "%s(%d): %s (code %d)\n", __FILENAME__, __LINE__, string_VkResult(static_cast<VkResult>(res)), res); \
	throw std::runtime_error("Exited due to Vulkan error");  \
}

#define EXIT_ON_VULKAN_NON_SUCCESS(res) \
if (vk::Result(res) != vk::Result::eSuccess) { \
	fprintf(stderr, "%s(%d): %s (code %d)\n", __FILENAME__, __LINE__, string_VkResult(static_cast<VkResult>(res)), res); \
	throw std::runtime_error("Exited due to Vulkan non success");  \
}

#define LOG_ERROR(fmt, ...) fprintf(stderr, "Error: " fmt "\n", __VA_ARGS__);
#define LOG_INFO(fmt, ...) fprintf(stdout, "Info: " fmt "\n", __VA_ARGS__);