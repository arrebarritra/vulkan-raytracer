#include <image.h>

namespace vkrt {

Image::Image(vk::SharedDevice device, DeviceMemoryManager& dmm, ResourceTransferHandler& rth, vk::ImageCreateInfo imageCI, vk::ArrayProxyNoTemporaries<char> data,
			 vk::ImageLayout targetLayout, const vk::MemoryPropertyFlags& memProps, DeviceMemoryManager::AllocationStrategy as)
	: ManagedResource(device, dmm, rth, memProps,
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

Image::Image(vk::SharedDevice device, DeviceMemoryManager& dmm, ResourceTransferHandler& rth, vk::ImageCreateInfo imageCI, std::filesystem::path imageFile,
			 vk::ImageLayout targetLayout, const vk::MemoryPropertyFlags& memProps, DeviceMemoryManager::AllocationStrategy as)
	: ManagedResource(device, dmm, rth, memProps,
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
		auto staged = std::make_unique<Buffer>(device, dmm, rth, stagedBufferCI, data, MemoryStorage::HostStaging);

		auto bfrImgCp = vk::BufferImageCopy{}
			.setBufferOffset(0u)
			.setBufferRowLength(imageCI.extent.width)
			.setBufferImageHeight(imageCI.extent.height)
			.setImageExtent(imageCI.extent)
			.setImageOffset({ 0u, 0u, 0u })
			.setImageSubresource({ vk::ImageAspectFlagBits::eColor, 0u, 0u, imageCI.arrayLayers });

		auto& stagedBuffer = *staged;
		SyncInfo si{ vk::SharedFence(device->createFence({}), device), {}, {} };
		copyFrom(stagedBuffer, bfrImgCp, targetLayout, si, std::move(staged));
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
		stagedImageCI
			.setUsage(vk::ImageUsageFlagBits::eTransferDst)
			.setTiling(vk::ImageTiling::eLinear);
		auto staged = std::make_unique<Image>(device, dmm, rth, stagedImageCI, nullptr, vk::ImageLayout::eTransferDstOptimal, MemoryStorage::HostDownload);

		auto imgCp = vk::ImageCopy{}
			.setExtent(imageCI.extent)
			.setSrcOffset({ 0u, 0u, 0u })
			.setSrcSubresource({ vk::ImageAspectFlagBits::eColor, 0u, 0u, imageCI.arrayLayers }) // TODO: calculate aspect from format
			.setDstOffset({ 0u, 0u, 0u })
			.setDstSubresource({ vk::ImageAspectFlagBits::eColor, 0u, 0u, imageCI.arrayLayers });

		auto& stagedBuffer = *staged;
		SyncInfo si{ vk::SharedFence(device->createFence({}), device), {}, {} };
		copyTo(stagedBuffer, imgCp, vk::ImageLayout::eGeneral, si, std::move(staged));
		rth.flushPendingTransfers(readFinishedFence);
		return staged->read();
	}
}

void Image::copyFrom(vk::Image srcImage, vk::ImageCopy imgCp, vk::ImageLayout layout, SyncInfo si, std::unique_ptr<ManagedResource> stagedResource) {
	if (imageCI.usage & vk::ImageUsageFlagBits::eTransferSrc)
		CHECK_VULKAN_RESULT(device->waitForFences(*readFinishedFence, vk::True, std::numeric_limits<uint64_t>::max())); // wait before reads from current image have finished before writing

	rth.copy(srcImage, *image, imgCp, layout, si, std::move(stagedResource));
	writeFinishedFence = si.fence;
}

void Image::copyFrom(Image& srcImage, vk::ImageCopy imgCp, vk::ImageLayout layout, SyncInfo si, std::unique_ptr<ManagedResource> stagedResource) {
	if (imageCI.usage & vk::ImageUsageFlagBits::eTransferSrc)
		CHECK_VULKAN_RESULT(device->waitForFences(*readFinishedFence, vk::True, std::numeric_limits<uint64_t>::max())); // wait before reads from current image have finished before writing

	rth.copy(*srcImage, *image, imgCp, layout, si, std::move(stagedResource));
	writeFinishedFence = si.fence;
	srcImage.readFinishedFence = writeFinishedFence;
}

void Image::copyFrom(vk::Buffer srcBuffer, vk::BufferImageCopy bfrImgCp, vk::ImageLayout layout, SyncInfo si, std::unique_ptr<ManagedResource> stagedResource) {
	if (imageCI.usage & vk::ImageUsageFlagBits::eTransferSrc)
		CHECK_VULKAN_RESULT(device->waitForFences(*readFinishedFence, vk::True, std::numeric_limits<uint64_t>::max())); // wait before reads from current image have finished before writing

	rth.copy(srcBuffer, *image, bfrImgCp, layout, si, std::move(stagedResource));
	writeFinishedFence = si.fence;
}

void Image::copyFrom(Buffer& srcBuffer, vk::BufferImageCopy bfrImgCp, vk::ImageLayout layout, SyncInfo si, std::unique_ptr<ManagedResource> stagedResource) {
	if (imageCI.usage & vk::ImageUsageFlagBits::eTransferSrc)
		CHECK_VULKAN_RESULT(device->waitForFences(*readFinishedFence, vk::True, std::numeric_limits<uint64_t>::max())); // wait before reads from current image have finished before writing

	rth.copy(*srcBuffer, *image, bfrImgCp, layout, si, std::move(stagedResource));
	writeFinishedFence = si.fence;
	srcBuffer.readFinishedFence = writeFinishedFence;
}

void Image::copyTo(vk::Image dstImage, vk::ImageCopy imgCp, vk::ImageLayout dstLayout, SyncInfo si, std::unique_ptr<ManagedResource> stagedResource) {
	if (imageCI.usage & vk::ImageUsageFlagBits::eTransferDst)
		CHECK_VULKAN_RESULT(device->waitForFences(*writeFinishedFence, vk::True, std::numeric_limits<uint64_t>::max())); // wait before writes into current image has finished before reading

	rth.copy(*image, dstImage, imgCp, dstLayout, si, std::move(stagedResource));
	readFinishedFence = si.fence;
}

void Image::copyTo(Image& dstImage, vk::ImageCopy imgCp, vk::ImageLayout dstLayout, SyncInfo si, std::unique_ptr<ManagedResource> stagedResource) {
	if (imageCI.usage & vk::ImageUsageFlagBits::eTransferDst)
		CHECK_VULKAN_RESULT(device->waitForFences(*writeFinishedFence, vk::True, std::numeric_limits<uint64_t>::max())); // wait before writes into current image has finished before reading

	rth.copy(*image, *dstImage.image, imgCp, dstLayout, si, std::move(stagedResource));
	readFinishedFence = si.fence;
	dstImage.writeFinishedFence = readFinishedFence;
}

void Image::copyTo(vk::Buffer dstBuffer, vk::BufferImageCopy bfrImgCp, SyncInfo si, std::unique_ptr<ManagedResource> stagedResource) {
	if (imageCI.usage & vk::ImageUsageFlagBits::eTransferDst)
		CHECK_VULKAN_RESULT(device->waitForFences(*writeFinishedFence, vk::True, std::numeric_limits<uint64_t>::max())); // wait before writes into current image has finished before reading

	rth.copy(*image, dstBuffer, bfrImgCp, si, std::move(stagedResource));
	readFinishedFence = si.fence;
}

void Image::copyTo(Buffer& dstBuffer, vk::BufferImageCopy bfrImgCp, SyncInfo si, std::unique_ptr<ManagedResource> stagedResource) {
	if (imageCI.usage & vk::ImageUsageFlagBits::eTransferDst)
		CHECK_VULKAN_RESULT(device->waitForFences(*writeFinishedFence, vk::True, std::numeric_limits<uint64_t>::max())); // wait before writes into current image has finished before reading

	rth.copy(*image, *dstBuffer.buffer, bfrImgCp, si, std::move(stagedResource));
	readFinishedFence = si.fence;
	dstBuffer.writeFinishedFence = readFinishedFence;
}


}