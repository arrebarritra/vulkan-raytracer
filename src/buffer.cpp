#include <buffer.h>

namespace vkrt {

Buffer::Buffer(vk::SharedDevice device, DeviceMemoryManager& dmm, ResourceTransferHandler& rth, vk::BufferCreateInfo bufferCI, vk::ArrayProxyNoTemporaries<char> data, const vk::MemoryPropertyFlags& memProps, DeviceMemoryManager::AllocationStrategy as)
	: ManagedResource(device, dmm, rth, memProps,
					  static_cast<bool>(bufferCI.usage& vk::BufferUsageFlagBits::eTransferSrc),
					  static_cast<bool>(bufferCI.usage& vk::BufferUsageFlagBits::eTransferDst) || (data.size() != 0 && !(memProps & vk::MemoryPropertyFlagBits::eHostVisible)))
	, bufferCI(bufferCI
			   .setUsage(bufferCI.usage | ((data.size() != 0 && !(memProps & vk::MemoryPropertyFlagBits::eHostVisible)) ? vk::BufferUsageFlagBits::eTransferDst : vk::BufferUsageFlags{})))
	, buffer(device->createBufferUnique(bufferCI))
{
	auto memReqs = device->getBufferMemoryRequirements(*buffer);
	memBlock = dmm.allocateResource(memReqs, memProps, as);
	device->bindBufferMemory(*buffer, *memBlock->allocation.memory, memBlock->offset);

	if (data.size() != 0) write(data);
}

// If fence is returned the resource copy handler command buffer needs to be manually submitted
std::optional<vk::SharedFence> Buffer::write(vk::ArrayProxyNoTemporaries<char> data, vk::DeviceSize offset) {
	assert(offset + data.size() <= memBlock->size);
	if (memBlock->mapping) {
		// Map memory
		memcpy(memBlock->mapping + offset, data.data(), data.size());
		if (!(memBlock->allocation.memProps & vk::MemoryPropertyFlagBits::eHostCoherent))
			device->flushMappedMemoryRanges(vk::MappedMemoryRange{}
											.setMemory(*memBlock->allocation.memory)
											.setOffset(memBlock->offset + offset)
											.setSize(data.size()));
		return std::nullopt;
	} else {
		// Create and read from staging buffer
		auto stagedBufferCI = bufferCI;
		stagedBufferCI.setUsage(vk::BufferUsageFlagBits::eTransferSrc);
		auto staged = std::make_unique<Buffer>(device, dmm, rth, stagedBufferCI, nullptr, MemoryStorage::HostStaging);
		staged->write(data, offset);
		auto bfrCp = vk::BufferCopy{}
			.setSize(data.size())
			.setSrcOffset(offset)
			.setDstOffset(offset);

		auto& stagedBuffer = *staged;
		SyncInfo si{ vk::SharedFence(device->createFence({}), device), {}, {} };
		copyFrom(stagedBuffer, bfrCp, si, std::move(staged));
		return writeFinishedFence;
	}
}

std::vector<char> Buffer::read() {
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
		auto stagedBufferCI = bufferCI;
		stagedBufferCI.setUsage(vk::BufferUsageFlagBits::eTransferDst);
		auto staged = std::make_unique<Buffer>(device, dmm, rth, stagedBufferCI, nullptr, MemoryStorage::HostDownload);
		auto bfrCp = vk::BufferCopy{}.setSrcOffset(0u).setDstOffset(0u).setSize(memBlock->size);

		auto& stagedBuffer = *staged;
		SyncInfo si{ vk::SharedFence(device->createFence({}), device), {}, {} };
		copyTo(stagedBuffer, bfrCp, si, std::move(staged));
		rth.flushPendingTransfers(readFinishedFence);
		return staged->read();
	}
}

void Buffer::copyFrom(vk::Buffer srcBuffer, vk::BufferCopy bfrCp, SyncInfo si, std::unique_ptr<ManagedResource> stagedResource) {
	if (bufferCI.usage & vk::BufferUsageFlagBits::eTransferSrc)
		CHECK_VULKAN_RESULT(device->waitForFences(*readFinishedFence, vk::True, std::numeric_limits<uint64_t>::max())); // wait before reads from current image have finished before writing

	rth.copy(srcBuffer, *buffer, bfrCp, si, std::move(stagedResource));
	writeFinishedFence = si.fence;
}

void Buffer::copyFrom(Buffer& srcBuffer, vk::BufferCopy bfrCp, SyncInfo si, std::unique_ptr<ManagedResource> stagedResource) {
	if (bufferCI.usage & vk::BufferUsageFlagBits::eTransferSrc)
		CHECK_VULKAN_RESULT(device->waitForFences(*readFinishedFence, vk::True, std::numeric_limits<uint64_t>::max()));

	rth.copy(*srcBuffer, *buffer, bfrCp, si, std::move(stagedResource));
	writeFinishedFence = si.fence;
	srcBuffer.readFinishedFence = writeFinishedFence;
}

void Buffer::copyFrom(vk::Image srcImage, vk::BufferImageCopy bfrImgCp, SyncInfo si, std::unique_ptr<ManagedResource> stagedResource) {
	if (bufferCI.usage & vk::BufferUsageFlagBits::eTransferSrc)
		CHECK_VULKAN_RESULT(device->waitForFences(*readFinishedFence, vk::True, std::numeric_limits<uint64_t>::max()));

	rth.copy(srcImage, *buffer, bfrImgCp, si, std::move(stagedResource));
	writeFinishedFence = si.fence;
}

void Buffer::copyFrom(Image& srcImage, vk::BufferImageCopy bfrImgCp, SyncInfo si, std::unique_ptr<ManagedResource> stagedResource) {
	if (bufferCI.usage & vk::BufferUsageFlagBits::eTransferSrc)
		CHECK_VULKAN_RESULT(device->waitForFences(*readFinishedFence, vk::True, std::numeric_limits<uint64_t>::max()));

	rth.copy(*srcImage, *buffer, bfrImgCp, si, std::move(stagedResource));
	writeFinishedFence = si.fence;
	srcImage.readFinishedFence = writeFinishedFence;
}

void Buffer::copyTo(vk::Buffer dstBuffer, vk::BufferCopy bfrCp, SyncInfo si, std::unique_ptr<ManagedResource> stagedResource) {
	if (bufferCI.usage & vk::BufferUsageFlagBits::eTransferDst)
		CHECK_VULKAN_RESULT(device->waitForFences(*writeFinishedFence, vk::True, std::numeric_limits<uint64_t>::max())); // wait before writes to current image have finished before reading

	rth.copy(*buffer, dstBuffer, bfrCp, si, std::move(stagedResource));
	readFinishedFence = si.fence;
}

void Buffer::copyTo(Buffer& dstBuffer, vk::BufferCopy bfrCp, SyncInfo si, std::unique_ptr<ManagedResource> stagedResource) {
	if (bufferCI.usage & vk::BufferUsageFlagBits::eTransferDst)
		CHECK_VULKAN_RESULT(device->waitForFences(*writeFinishedFence, vk::True, std::numeric_limits<uint64_t>::max()));

	rth.copy(*buffer, *dstBuffer.buffer, bfrCp, si, std::move(stagedResource));
	readFinishedFence = si.fence;
	dstBuffer.writeFinishedFence = readFinishedFence;
}

void Buffer::copyTo(vk::Image dstImage, vk::BufferImageCopy bfrImgCp, vk::ImageLayout dstLayout, SyncInfo si, std::unique_ptr<ManagedResource> stagedResource) {
	if (bufferCI.usage & vk::BufferUsageFlagBits::eTransferDst)
		CHECK_VULKAN_RESULT(device->waitForFences(*writeFinishedFence, vk::True, std::numeric_limits<uint64_t>::max()));

	rth.copy(*buffer, dstImage, bfrImgCp, dstLayout, si, std::move(stagedResource));
	readFinishedFence = si.fence;
}

void Buffer::copyTo(Image& dstImage, vk::BufferImageCopy bfrImgCp, vk::ImageLayout dstLayout, SyncInfo si, std::unique_ptr<ManagedResource> stagedResource) {
	if (bufferCI.usage & vk::BufferUsageFlagBits::eTransferDst)
		CHECK_VULKAN_RESULT(device->waitForFences(*writeFinishedFence, vk::True, std::numeric_limits<uint64_t>::max()));

	rth.copy(*buffer, *dstImage.image, bfrImgCp, dstLayout, si, std::move(stagedResource));
	readFinishedFence = si.fence;
	dstImage.writeFinishedFence = readFinishedFence;
}


}