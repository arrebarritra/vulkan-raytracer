#include <image.h>
#include <resourcecopyhandler.h>

namespace vkrt {

Image::Image(vk::SharedDevice device, DeviceMemoryManager& dmm, ResourceCopyHandler& rch, vk::ImageCreateInfo imageCI, vk::ArrayProxyNoTemporaries<char> data, const vk::MemoryPropertyFlags& memProps, DeviceMemoryManager::AllocationStrategy as)
	: ManagedResource(device, dmm, rch, memProps)
	, imageCI(imageCI
			.setUsage(imageCI.usage | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst)  // Force transfer flags to be set to enable copying
			.setTiling(vk::ImageTiling::eOptimal)) // Only allow optimal tiling images for simplicity, defer to buffer for linear images
	, image(device->createImageUnique(imageCI))
{
	auto memReqs = device->getImageMemoryRequirements(*image);
	memBlock = dmm.allocateResource(memReqs, memProps, as);
	device->bindImageMemory(*image, *memBlock->allocation.memory, memBlock->offset);
	if (data.size() != 0) write(data);
}

Image::~Image() {
	// Complete pending submits
	if (device->getFenceStatus(*readFinishedFence) == vk::Result::eNotReady || device->getFenceStatus(*writeFinishedFence) == vk::Result::eNotReady) {
		device->waitForFences(*rch.submit(), vk::True, std::numeric_limits<uint64_t>::max());
	}
}

std::optional<vk::SharedFence> Image::write(vk::ArrayProxyNoTemporaries<char> data) {
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
		auto stagingImageCI = imageCI;
		stagingImageCI
			.setUsage(vk::ImageUsageFlagBits::eTransferSrc)
			.setInitialLayout(vk::ImageLayout::eTransferSrcOptimal);
		auto staged = Image(device, dmm, rch, stagingImageCI, data, MemoryStorage::HostStaging);

		auto imgCp = vk::ImageCopy{}
			.setExtent(imageCI.extent)
			.setSrcOffset({ 0u, 0u, 0u })
			.setSrcSubresource({ vk::ImageAspectFlagBits::eColor, 0u, 0u, imageCI.arrayLayers }) // TODO: calculate aspect from format
			.setDstOffset({ 0u, 0u, 0u })
			.setDstSubresource({ vk::ImageAspectFlagBits::eColor, 0u, 0u, imageCI.arrayLayers });
		writeFinishedFence = copyFrom(staged, imgCp);
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
		auto stagingImageCI = imageCI;
		stagingImageCI.setInitialLayout(vk::ImageLayout::eTransferDstOptimal);
		auto staged = Image(device, dmm, rch, stagingImageCI, nullptr, MemoryStorage::HostDownload);

		auto imgCp = vk::ImageCopy{}
			.setExtent(imageCI.extent)
			.setSrcOffset({ 0u, 0u, 0u })
			.setSrcSubresource({ vk::ImageAspectFlagBits::eColor, 0u, 0u, imageCI.arrayLayers }) // TODO: calculate aspect from format
			.setDstOffset({ 0u, 0u, 0u })
			.setDstSubresource({ vk::ImageAspectFlagBits::eColor, 0u, 0u, imageCI.arrayLayers });
		copyTo(staged, imgCp);

		CHECK_VULKAN_RESULT(device->waitForFences(*readFinishedFence, vk::True, std::numeric_limits<uint64_t>::max())); // wait until copy to staging buffer has finished
		return staged.read();
	}
}

vk::SharedFence Image::copyFrom(vk::Image srcImage, vk::ImageCopy imgCp, vk::ImageLayout layout) {
	CHECK_VULKAN_RESULT(device->waitForFences(*readFinishedFence, vk::True, std::numeric_limits<uint64_t>::max())); // wait before reads from current image have finished before writing
	writeFinishedFence = rch.recordCopyCmd(srcImage, *image, imgCp, layout);
	return writeFinishedFence;
}

vk::SharedFence Image::copyFrom(Image& srcImage, vk::ImageCopy imgCp, vk::ImageLayout layout) {
	CHECK_VULKAN_RESULT(device->waitForFences(*readFinishedFence, vk::True, std::numeric_limits<uint64_t>::max())); // wait before reads from current image have finished before writing
	writeFinishedFence = rch.recordCopyCmd(*srcImage.image, *image, imgCp, layout);
	srcImage.readFinishedFence = writeFinishedFence;
	return writeFinishedFence;
}

vk::SharedFence Image::copyFrom(vk::Buffer srcBuffer, vk::BufferImageCopy bfrImgCp, vk::ImageLayout layout) {
	CHECK_VULKAN_RESULT(device->waitForFences(*readFinishedFence, vk::True, std::numeric_limits<uint64_t>::max())); // wait before reads from current image have finished before writing
	writeFinishedFence = rch.recordCopyCmd(srcBuffer, *image, bfrImgCp, layout);
	return writeFinishedFence;
}

vk::SharedFence Image::copyFrom(Buffer& srcBuffer, vk::BufferImageCopy bfrImgCp, vk::ImageLayout layout) {
	CHECK_VULKAN_RESULT(device->waitForFences(*readFinishedFence, vk::True, std::numeric_limits<uint64_t>::max())); // wait before reads from current image have finished before writing
	writeFinishedFence = rch.recordCopyCmd(*srcBuffer.buffer, *image, bfrImgCp, layout);
	srcBuffer.readFinishedFence = writeFinishedFence;
	return writeFinishedFence;
}

vk::SharedFence Image::copyTo(vk::Image dstImage, vk::ImageCopy imgCp, vk::ImageLayout dstLayout) {
	CHECK_VULKAN_RESULT(device->waitForFences(*writeFinishedFence, vk::True, std::numeric_limits<uint64_t>::max())); // wait before writes into current image has finished before reading
	readFinishedFence = rch.recordCopyCmd(*image, dstImage, imgCp, dstLayout);
	return readFinishedFence;
}

vk::SharedFence Image::copyTo(Image& dstImage, vk::ImageCopy imgCp, vk::ImageLayout dstLayout) {
	CHECK_VULKAN_RESULT(device->waitForFences(*writeFinishedFence, vk::True, std::numeric_limits<uint64_t>::max())); // wait before writes into current image has finished before reading
	readFinishedFence = rch.recordCopyCmd(*image, *dstImage.image, imgCp, dstLayout);
	dstImage.writeFinishedFence = readFinishedFence;
	return readFinishedFence;
}

vk::SharedFence Image::copyTo(vk::Buffer dstBuffer, vk::BufferImageCopy bfrImgCp) {
	CHECK_VULKAN_RESULT(device->waitForFences(*writeFinishedFence, vk::True, std::numeric_limits<uint64_t>::max())); // wait before writes into current image has finished before reading
	readFinishedFence = rch.recordCopyCmd(*image, dstBuffer, bfrImgCp);
	return readFinishedFence;
}

vk::SharedFence Image::copyTo(Buffer& dstBuffer, vk::BufferImageCopy bfrImgCp) {
	CHECK_VULKAN_RESULT(device->waitForFences(*writeFinishedFence, vk::True, std::numeric_limits<uint64_t>::max())); // wait before writes into current image has finished before reading
	readFinishedFence = rch.recordCopyCmd(*image, *dstBuffer.buffer, bfrImgCp);
	dstBuffer.writeFinishedFence = readFinishedFence;
	return readFinishedFence;
}


}