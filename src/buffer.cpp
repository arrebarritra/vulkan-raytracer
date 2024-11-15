#include <buffer.h>

namespace vkrt {

Buffer::Buffer(vk::SharedDevice device, DeviceMemoryManager& dmm, ResourceCopyHandler& rch, vk::BufferCreateInfo bufferCI, vk::ArrayProxyNoTemporaries<char> data, const vk::MemoryPropertyFlags& memProps, DeviceMemoryManager::AllocationStrategy as)
	: ManagedResource(device, dmm, rch, memProps)
	, bufferCI(bufferCI
			   .setUsage(bufferCI.usage | vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst))  // Force transfer flags to be set to enable copying
	, buffer(device->createBufferUnique(bufferCI))
{
	auto& memReqs = device->getBufferMemoryRequirements(*buffer);
	memBlock = dmm.allocateResource(memReqs, memProps, as);
	device->bindBufferMemory(*buffer, *memBlock->allocation.memory, memBlock->offset);
	if (data.size() != 0) write(data);
}

Buffer::~Buffer() {
	// Submit any pending work
	if (device->getFenceStatus(*readFinishedFence) == vk::Result::eSuccess || device->getFenceStatus(*writeFinishedFence) == vk::Result::eSuccess) {
		device->waitForFences(*rch.submit(), vk::True, std::numeric_limits<uint64_t>::max());
	}
}

// If fence is returned the resource copy handler command buffer needs to be manually submitted
std::optional<vk::SharedFence> Buffer::write(vk::ArrayProxyNoTemporaries<char> data)
{
	// Copy buffer contents
	auto& memReqs = device->getBufferMemoryRequirements(buffer.get());

	if (memBlock->mapping) {
		// Map memory
		assert(data.size() <= memBlock->size); // not necessarily equal because of alignment accomodations
		memcpy(memBlock->mapping, data.data(), memBlock->size);
		if (!(memBlock->allocation.memProps & vk::MemoryPropertyFlagBits::eHostCoherent))
			device->flushMappedMemoryRanges(vk::MappedMemoryRange{}
											.setMemory(*memBlock->allocation.memory)
											.setOffset(memBlock->offset)
											.setSize(memBlock->size));
		return std::nullopt;
	} else {
		// Create and read from staging buffer
		auto& staged = Buffer(device, dmm, rch, bufferCI, data, MemoryStorage::HostStaging);
		auto& bfrCp = vk::BufferCopy{}.setSrcOffset(0u).setDstOffset(0u).setSize(memBlock->size);
		writeFinishedFence = copyFrom(staged, bfrCp);
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
		auto& staged = Buffer(device, dmm, rch, bufferCI, nullptr, MemoryStorage::HostDownload);
		auto& bfrCp = vk::BufferCopy{}.setSrcOffset(0u).setDstOffset(0u).setSize(memBlock->size);
		copyTo(staged, bfrCp);
		readFinishedFence = rch.submit();
		CHECK_VULKAN_RESULT(device->waitForFences(*readFinishedFence, vk::True, std::numeric_limits<uint64_t>::max())); // wait until copy to staging buffer has finished
		return staged.read();
	}
}

vk::SharedFence Buffer::copyFrom(vk::Buffer srcBuffer, vk::BufferCopy bfrCp) {
	CHECK_VULKAN_RESULT(device->waitForFences(*readFinishedFence, vk::True, std::numeric_limits<uint64_t>::max())); // wait before reads from current image have finished before writing
	writeFinishedFence = rch.recordCopyCmd(srcBuffer, *buffer, bfrCp);
	return writeFinishedFence;
}

vk::SharedFence Buffer::copyFrom(Buffer& srcBuffer, vk::BufferCopy bfrCp) {
	CHECK_VULKAN_RESULT(device->waitForFences(*readFinishedFence, vk::True, std::numeric_limits<uint64_t>::max()));
	writeFinishedFence = rch.recordCopyCmd(*srcBuffer.buffer, *buffer, bfrCp);
	srcBuffer.readFinishedFence = writeFinishedFence;
	return writeFinishedFence;
}

vk::SharedFence Buffer::copyFrom(vk::Image srcImage, vk::BufferImageCopy bfrImgCp) {
	CHECK_VULKAN_RESULT(device->waitForFences(*readFinishedFence, vk::True, std::numeric_limits<uint64_t>::max()));
	writeFinishedFence = rch.recordCopyCmd(srcImage, *buffer, bfrImgCp);
	return writeFinishedFence;
}

vk::SharedFence Buffer::copyFrom(Image& srcImage, vk::BufferImageCopy bfrImgCp) {
	CHECK_VULKAN_RESULT(device->waitForFences(*readFinishedFence, vk::True, std::numeric_limits<uint64_t>::max()));
	writeFinishedFence = rch.recordCopyCmd(*srcImage.image, *buffer, bfrImgCp);
	srcImage.readFinishedFence = writeFinishedFence;
	return writeFinishedFence;
}

vk::SharedFence Buffer::copyTo(vk::Buffer dstBuffer, vk::BufferCopy bfrCp) {
	CHECK_VULKAN_RESULT(device->waitForFences(*writeFinishedFence, vk::True, std::numeric_limits<uint64_t>::max())); // wait before writes to current image have finished before reading
	readFinishedFence = rch.recordCopyCmd(*buffer, dstBuffer, bfrCp);
	return readFinishedFence;
}

vk::SharedFence Buffer::copyTo(Buffer& dstBuffer, vk::BufferCopy bfrCp) {
	CHECK_VULKAN_RESULT(device->waitForFences(*writeFinishedFence, vk::True, std::numeric_limits<uint64_t>::max()));
	readFinishedFence = rch.recordCopyCmd(*buffer, *dstBuffer.buffer, bfrCp);
	dstBuffer.writeFinishedFence = readFinishedFence;
	return readFinishedFence;
}

vk::SharedFence Buffer::copyTo(vk::Image dstImage, vk::BufferImageCopy bfrImgCp, vk::ImageLayout dstLayout) {
	CHECK_VULKAN_RESULT(device->waitForFences(*readFinishedFence, vk::True, std::numeric_limits<uint64_t>::max()));
	readFinishedFence = rch.recordCopyCmd(*buffer, dstImage, bfrImgCp, dstLayout);
	return readFinishedFence;
}

vk::SharedFence Buffer::copyTo(Image& dstImage, vk::BufferImageCopy bfrImgCp, vk::ImageLayout dstLayout) {
	CHECK_VULKAN_RESULT(device->waitForFences(*readFinishedFence, vk::True, std::numeric_limits<uint64_t>::max()));
	readFinishedFence = rch.recordCopyCmd(*buffer, *dstImage.image, bfrImgCp, dstLayout);
	dstImage.writeFinishedFence = readFinishedFence;
	return readFinishedFence;
}


}