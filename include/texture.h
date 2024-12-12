#include <vulkan_headers.h>
#include <filesystem>
#include <image.h>

namespace vkrt {

class Texture {

public:
	Texture(vk::SharedDevice device, DeviceMemoryManager& dmm, ResourceCopyHandler& rch, std::filesystem::path imageFile, DeviceMemoryManager::AllocationStrategy as = DeviceMemoryManager::AllocationStrategy::Heuristic);
	const vk::DescriptorImageInfo getDescriptor();

	Image image;
	vk::UniqueSampler sampler;
	vk::UniqueImageView view;
};

}