#include <image.h>
#include <resourcecopyhandler.h>

namespace vkrt {

Image::Image(vk::SharedDevice device, DeviceMemoryManager& dmm, ResourceCopyHandler& rch, vk::ImageCreateInfo imageCI, vk::ArrayProxyNoTemporaries<char> data,
			 vk::ImageLayout targetLayout, const vk::MemoryPropertyFlags& memProps, DeviceMemoryManager::AllocationStrategy as)
	: ManagedResource(device, dmm, rch, memProps,
					  static_cast<bool>(imageCI.usage& vk::ImageUsageFlagBits::eTransferSrc),
					  static_cast<bool>(imageCI.usage& vk::ImageUsageFlagBits::eTransferDst) || (data.size() != 0 && !(memProps & vk::MemoryPropertyFlagBits::eHostVisible)))
	, imageCI(imageCI
			  .setUsage(imageCI.usage |
						((data.size() != 0 && !(memProps & vk::MemoryPropertyFlagBits::eHostVisible)) ? vk::ImageUsageFlagBits::eTransferDst : vk::ImageUsageFlags{})))
	, image(device->createImageUnique(imageCI))
{
	auto memReqs = device->getImageMemoryRequirements(*image);
	memBlock = dmm.allocateResource(memReqs, memProps, as);
	device->bindImageMemory(*image, *memBlock->allocation.memory, memBlock->offset);
	if (data.size() != 0) write(data);
}

Image::Image(vk::SharedDevice device, DeviceMemoryManager& dmm, ResourceCopyHandler& rch, vk::ImageCreateInfo imageCI, std::filesystem::path imageFile,
			 vk::ImageLayout targetLayout, const vk::MemoryPropertyFlags& memProps, DeviceMemoryManager::AllocationStrategy as)
	: ManagedResource(device, dmm, rch, memProps,
					  static_cast<bool>(imageCI.usage& vk::ImageUsageFlagBits::eTransferSrc),
					  static_cast<bool>(imageCI.usage& vk::ImageUsageFlagBits::eTransferDst) || !(memProps & vk::MemoryPropertyFlagBits::eHostVisible))
	, imageCI(imageCI
			  .setUsage(imageCI.usage |
						(!(memProps & vk::MemoryPropertyFlagBits::eHostVisible) ? vk::ImageUsageFlagBits::eTransferDst : vk::ImageUsageFlags{})))
{
	int x, y, n;
	stbi_info(imageFile.string().c_str(), &x, &y, &n);
	int requiredComponents = n == 3 ? 4 : n;
	stbi_uc* imageData = stbi_load(imageFile.string().c_str(), &x, &y, &n, requiredComponents);
	this->imageCI.setExtent(vk::Extent3D{ static_cast<uint32_t>(x), static_cast<uint32_t>(y), 1u });
	switch (requiredComponents) {
		case 1:
			this->imageCI.setFormat(vk::Format::eR8Unorm);
			break;
		case 2:
			this->imageCI.setFormat(vk::Format::eR8G8Unorm);
			break;
		case 4:
			this->imageCI.setFormat(vk::Format::eR8G8B8A8Unorm);
			break;
	}

	image = device->createImageUnique(this->imageCI);
	auto memReqs = device->getImageMemoryRequirements(*image);
	memBlock = dmm.allocateResource(memReqs, memProps, as);
	device->bindImageMemory(*image, *memBlock->allocation.memory, memBlock->offset);

	write({ static_cast<uint32_t>(x * y * requiredComponents), (char*)imageData });
	stbi_image_free(imageData);
}

std::optional<vk::SharedFence> Image::write(vk::ArrayProxyNoTemporaries<char> data, vk::ImageLayout targetLayout) {
	auto memReqs = device->getImageMemoryRequirements(*image);

	if (memBlock->mapping) {
		// Map memory
		assert(data.size() < memBlock->size); // not necessarily equal because of alignment accomodations
		memcpy(memBlock->mapping, data.data(), memBlock->size);
		if (!(memBlock->allocation.memProps & vk::MemoryPropertyFlagBits::eHostCoherent))
			device->flushMappedMemoryRanges(vk::MappedMemoryRange{}
											.setMemory(*memBlock->allocation.memory)
											.setOffset(memBlock->offset)
											.setSize(memBlock->size));
		return std::nullopt;
	} else {
		// Create and read from staging buffer
		auto stagedBufferCI = vk::BufferCreateInfo{}
			.setSize(data.size())
			.setUsage(vk::BufferUsageFlagBits::eTransferSrc);
		auto staged = Buffer(device, dmm, rch, stagedBufferCI, data, MemoryStorage::HostStaging);

		auto bfrImgCp = vk::BufferImageCopy{}
			.setBufferOffset(0u)
			.setBufferRowLength(imageCI.extent.width)
			.setBufferImageHeight(imageCI.extent.height)
			.setImageExtent(imageCI.extent)
			.setImageOffset({ 0u, 0u, 0u })
			.setImageSubresource({ vk::ImageAspectFlagBits::eColor, 0u, 0u, imageCI.arrayLayers });
		copyFrom(staged, bfrImgCp, targetLayout);
		writeFinishedFence = rch.submit();
		CHECK_VULKAN_RESULT(device->waitForFences(*writeFinishedFence, vk::True, std::numeric_limits<uint64_t>::max())); // wait until copy from staging buffer has finished
		return writeFinishedFence;
	}
}

std::vector<char> Image::read() {
	if (memBlock->mapping) {
		std::vector<char> data(memBlock->size);
		if (!(memBlock->allocation.memProps & vk::MemoryPropertyFlagBits::eHostCoherent))
			device->invalidateMappedMemoryRanges(vk::MappedMemoryRange{}
												 .setMemory(*memBlock->allocation.memory)
												 .setOffset(memBlock->offset)
												 .setSize(memBlock->size));
		memcpy(data.data(), memBlock->mapping, memBlock->size);
		return data;
	} else {
		// Create and write to staging buffer
		auto stagedImageCI = imageCI;
		stagedImageCI.setUsage(vk::ImageUsageFlagBits::eTransferDst);
		auto staged = Image(device, dmm, rch, stagedImageCI, nullptr, vk::ImageLayout::eTransferDstOptimal, MemoryStorage::HostDownload);

		auto imgCp = vk::ImageCopy{}
			.setExtent(imageCI.extent)
			.setSrcOffset({ 0u, 0u, 0u })
			.setSrcSubresource({ vk::ImageAspectFlagBits::eColor, 0u, 0u, imageCI.arrayLayers }) // TODO: calculate aspect from format
			.setDstOffset({ 0u, 0u, 0u })
			.setDstSubresource({ vk::ImageAspectFlagBits::eColor, 0u, 0u, imageCI.arrayLayers });
		copyTo(staged, imgCp);
		readFinishedFence = rch.submit();
		CHECK_VULKAN_RESULT(device->waitForFences(*readFinishedFence, vk::True, std::numeric_limits<uint64_t>::max())); // wait until copy to staging buffer has finished
		return staged.read();
	}
}

vk::SharedFence Image::copyFrom(vk::Image srcImage, vk::ImageCopy imgCp, vk::ImageLayout layout) {
	if (imageCI.usage & vk::ImageUsageFlagBits::eTransferSrc)
		CHECK_VULKAN_RESULT(device->waitForFences(*readFinishedFence, vk::True, std::numeric_limits<uint64_t>::max())); // wait before reads from current image have finished before writing
	writeFinishedFence = rch.recordCopyCmd(srcImage, *image, imgCp, layout);
	return writeFinishedFence;
}

vk::SharedFence Image::copyFrom(Image& srcImage, vk::ImageCopy imgCp, vk::ImageLayout layout) {
	if (imageCI.usage & vk::ImageUsageFlagBits::eTransferSrc)
		CHECK_VULKAN_RESULT(device->waitForFences(*readFinishedFence, vk::True, std::numeric_limits<uint64_t>::max())); // wait before reads from current image have finished before writing
	writeFinishedFence = rch.recordCopyCmd(*srcImage.image, *image, imgCp, layout);
	srcImage.readFinishedFence = writeFinishedFence;
	return writeFinishedFence;
}

vk::SharedFence Image::copyFrom(vk::Buffer srcBuffer, vk::BufferImageCopy bfrImgCp, vk::ImageLayout layout) {
	if (imageCI.usage & vk::ImageUsageFlagBits::eTransferSrc)
		CHECK_VULKAN_RESULT(device->waitForFences(*readFinishedFence, vk::True, std::numeric_limits<uint64_t>::max())); // wait before reads from current image have finished before writing
	writeFinishedFence = rch.recordCopyCmd(srcBuffer, *image, bfrImgCp, layout);
	return writeFinishedFence;
}

vk::SharedFence Image::copyFrom(Buffer& srcBuffer, vk::BufferImageCopy bfrImgCp, vk::ImageLayout layout) {
	if (imageCI.usage & vk::ImageUsageFlagBits::eTransferSrc)
		CHECK_VULKAN_RESULT(device->waitForFences(*readFinishedFence, vk::True, std::numeric_limits<uint64_t>::max())); // wait before reads from current image have finished before writing
	writeFinishedFence = rch.recordCopyCmd(*srcBuffer.buffer, *image, bfrImgCp, layout);
	srcBuffer.readFinishedFence = writeFinishedFence;
	return writeFinishedFence;
}

vk::SharedFence Image::copyTo(vk::Image dstImage, vk::ImageCopy imgCp, vk::ImageLayout dstLayout) {
	if (imageCI.usage & vk::ImageUsageFlagBits::eTransferDst)
		CHECK_VULKAN_RESULT(device->waitForFences(*writeFinishedFence, vk::True, std::numeric_limits<uint64_t>::max())); // wait before writes into current image has finished before reading
	readFinishedFence = rch.recordCopyCmd(*image, dstImage, imgCp, dstLayout);
	return readFinishedFence;
}

vk::SharedFence Image::copyTo(Image& dstImage, vk::ImageCopy imgCp, vk::ImageLayout dstLayout) {
	if (imageCI.usage & vk::ImageUsageFlagBits::eTransferDst)
		CHECK_VULKAN_RESULT(device->waitForFences(*writeFinishedFence, vk::True, std::numeric_limits<uint64_t>::max())); // wait before writes into current image has finished before reading
	readFinishedFence = rch.recordCopyCmd(*image, *dstImage.image, imgCp, dstLayout);
	dstImage.writeFinishedFence = readFinishedFence;
	return readFinishedFence;
}

vk::SharedFence Image::copyTo(vk::Buffer dstBuffer, vk::BufferImageCopy bfrImgCp) {
	if (imageCI.usage & vk::ImageUsageFlagBits::eTransferDst)
		CHECK_VULKAN_RESULT(device->waitForFences(*writeFinishedFence, vk::True, std::numeric_limits<uint64_t>::max())); // wait before writes into current image has finished before reading
	readFinishedFence = rch.recordCopyCmd(*image, dstBuffer, bfrImgCp);
	return readFinishedFence;
}

vk::SharedFence Image::copyTo(Buffer& dstBuffer, vk::BufferImageCopy bfrImgCp) {
	if (imageCI.usage & vk::ImageUsageFlagBits::eTransferDst)
		CHECK_VULKAN_RESULT(device->waitForFences(*writeFinishedFence, vk::True, std::numeric_limits<uint64_t>::max())); // wait before writes into current image has finished before reading
	readFinishedFence = rch.recordCopyCmd(*image, *dstBuffer.buffer, bfrImgCp);
	dstBuffer.writeFinishedFence = readFinishedFence;
	return readFinishedFence;
}


}