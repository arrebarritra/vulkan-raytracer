#include <texture.h>

namespace vkrt {

Texture::Texture(vk::SharedDevice device, DeviceMemoryManager& dmm, ResourceTransferHandler& rth, std::filesystem::path imageFile, DeviceMemoryManager::AllocationStrategy as)
	: image(device, dmm, rth, vk::ImageCreateInfo{}
			.setImageType(vk::ImageType::e2D)
			.setSamples(vk::SampleCountFlagBits::e1)
			.setUsage(vk::ImageUsageFlagBits::eSampled)
			.setArrayLayers(1u)
			.setMipLevels(1u),
			imageFile, vk::ImageLayout::eShaderReadOnlyOptimal, MemoryStorage::DevicePersistent)
{
	auto samplerCI = vk::SamplerCreateInfo{}
		.setMagFilter(vk::Filter::eLinear)
		.setMinFilter(vk::Filter::eLinear)
		.setMaxAnisotropy(1.0f)
		.setBorderColor(vk::BorderColor::eFloatTransparentBlack);
	sampler = device->createSamplerUnique(samplerCI);

	auto imageViewCI = vk::ImageViewCreateInfo{}
		.setViewType(static_cast<vk::ImageViewType>(image.imageCI.imageType))
		.setFormat(image.imageCI.format)
		.setSubresourceRange(vk::ImageSubresourceRange{}
							 .setAspectMask(vk::ImageAspectFlagBits::eColor)
							 .setBaseMipLevel(0u)
							 .setLevelCount(1u)
							 .setBaseArrayLayer(0u)
							 .setLayerCount(1u))
		.setImage(*image);
	view = device->createImageViewUnique(imageViewCI);
}

const vk::DescriptorImageInfo Texture::getDescriptor()
{
	return vk::DescriptorImageInfo{}
		.setSampler(*sampler)
		.setImageView(*view)
		.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
}

}