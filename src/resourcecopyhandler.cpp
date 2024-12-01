#include <resourcecopyhandler.h>

namespace vkrt {
ResourceCopyHandler::ResourceCopyHandler(vk::SharedDevice device, std::tuple<uint32_t, vk::Queue> transferQueue)
	: device(device), transferQueue(transferQueue)
{
	auto cmdPoolCI = vk::CommandPoolCreateInfo{}
		.setQueueFamilyIndex(std::get<uint32_t>(transferQueue))
		.setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
	commandPool = device->createCommandPoolUnique(cmdPoolCI);

	auto buffersTmp = device->allocateCommandBuffersUnique(vk::CommandBufferAllocateInfo{}
															.setCommandPool(*commandPool)
															.setCommandBufferCount(POOL_SIZE)
															.setLevel(vk::CommandBufferLevel::ePrimary));
	std::move(buffersTmp.begin(), buffersTmp.end(), transferCmdBuffers.begin());

	std::generate(transferFences.begin(), transferFences.end(), [device]() {
		return vk::SharedFence(device->createFence(vk::FenceCreateInfo{}.setFlags(vk::FenceCreateFlagBits::eSignaled)), device); });
}

const vk::SharedFence& ResourceCopyHandler::submit(vk::ArrayProxyNoTemporaries<vk::SharedSemaphore> waitSemaphores, vk::ArrayProxyNoTemporaries<vk::SharedSemaphore> signalSemaphores, std::optional<vk::SharedFence> fence) {
	if (recording) {
		transferCmdBuffers[idx]->end();
		recording = false;

		// Create submit info
		std::vector<vk::PipelineStageFlags> submitWaitDstStageMask(waitSemaphores.size());
		std::vector<vk::Semaphore> submitWaitSemaphores(waitSemaphores.size());
		std::vector<vk::Semaphore> submitSignalSemaphores(signalSemaphores.size());
		std::fill(submitWaitDstStageMask.begin(), submitWaitDstStageMask.end(), vk::PipelineStageFlagBits::eTransfer);
		std::transform(waitSemaphores.begin(), waitSemaphores.end(), submitWaitSemaphores.begin(),
					   [](const vk::SharedSemaphore& ws) { return *ws; });
		std::transform(signalSemaphores.begin(), signalSemaphores.end(), submitSignalSemaphores.begin(),
					   [](const vk::SharedSemaphore& ss) { return *ss; });

		auto submitInfo = vk::SubmitInfo{}
			.setCommandBuffers(*transferCmdBuffers[idx])
			.setWaitDstStageMask(submitWaitDstStageMask)
			.setWaitSemaphores(submitWaitSemaphores)
			.setSignalSemaphores(submitSignalSemaphores);

		// TODO: do not invalidate original fence
		if (fence) transferFences[idx] = *fence;
		device->resetFences(*transferFences[idx]);
		std::get<vk::Queue>(transferQueue).submit(submitInfo, *transferFences[idx]);
	}

	return transferFences[idx];
}

void ResourceCopyHandler::startRecording() {
	recording = true;

	std::array<vk::Fence, POOL_SIZE> fencesTmp;
	std::transform(transferFences.begin(), transferFences.end(), fencesTmp.begin(),
				   [](const vk::SharedFence& sf) { return *sf; });
	device->waitForFences(fencesTmp, vk::False, std::numeric_limits<uint64_t>::max());
	while (device->getFenceStatus(*transferFences[idx]) != vk::Result::eSuccess) ++idx %= POOL_SIZE;

	transferCmdBuffers[idx]->reset();
	transferCmdBuffers[idx]->begin(vk::CommandBufferBeginInfo{}.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
}

const vk::SharedFence& ResourceCopyHandler::recordCopyCmd(vk::Buffer srcBuffer, vk::Buffer dstBuffer, vk::BufferCopy bfrCp) {
	if (!recording) startRecording();

	transferCmdBuffers[idx]->copyBuffer(srcBuffer, dstBuffer, bfrCp);

	return transferFences[idx];
}

const vk::SharedFence& ResourceCopyHandler::recordCopyCmd(vk::Image srcImage, vk::Image dstImage, vk::ImageCopy imgCp, vk::ImageLayout dstLayout) {
	if (!recording) startRecording();

	// Set up pipeline barriers for image transitions
	auto srcPreImMemBarrier = vk::ImageMemoryBarrier{}
		.setDstAccessMask(vk::AccessFlagBits::eTransferRead)
		.setOldLayout(vk::ImageLayout::eUndefined)
		.setNewLayout(vk::ImageLayout::eTransferSrcOptimal)
		.setImage(srcImage)
		.setSubresourceRange({ imgCp.srcSubresource.aspectMask, imgCp.srcSubresource.mipLevel, 1u,
							 imgCp.srcSubresource.baseArrayLayer, imgCp.srcSubresource.layerCount });
	auto dstPreImMemBarrier = vk::ImageMemoryBarrier{}
		.setDstAccessMask(vk::AccessFlagBits::eTransferWrite)
		.setOldLayout(vk::ImageLayout::eUndefined)
		.setNewLayout(vk::ImageLayout::eTransferDstOptimal)
		.setImage(dstImage)
		.setSubresourceRange({ imgCp.srcSubresource.aspectMask, imgCp.srcSubresource.mipLevel, 1u,
							 imgCp.srcSubresource.baseArrayLayer, imgCp.srcSubresource.layerCount });
	std::array<vk::ImageMemoryBarrier, 2> preImMemBarriers{ srcPreImMemBarrier, dstPreImMemBarrier };
	transferCmdBuffers[idx]->pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
											 {}, {}, {}, preImMemBarriers);


	transferCmdBuffers[idx]->copyImage(srcImage, vk::ImageLayout::eTransferSrcOptimal, dstImage, vk::ImageLayout::eTransferDstOptimal, imgCp);

	if (dstLayout != vk::ImageLayout{}) {
		auto dstPostImMemBarrier = vk::ImageMemoryBarrier{}
			.setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
			.setOldLayout(vk::ImageLayout::eTransferDstOptimal)
			.setNewLayout(dstLayout)
			.setImage(dstImage)
			.setSubresourceRange({ imgCp.srcSubresource.aspectMask, imgCp.srcSubresource.mipLevel, 1u,
								 imgCp.srcSubresource.baseArrayLayer, imgCp.srcSubresource.layerCount });
		transferCmdBuffers[idx]->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eBottomOfPipe,
												 {}, {}, {}, dstPostImMemBarrier);
	}

	return transferFences[idx];
}

const vk::SharedFence& ResourceCopyHandler::recordCopyCmd(vk::Buffer srcBuffer, vk::Image dstImage, vk::BufferImageCopy bfrImgCp, vk::ImageLayout dstLayout) {
	if (!recording) startRecording();

	// Set up pipeline barriers for image transition
	auto dstImMemBarrier = vk::ImageMemoryBarrier{}
		.setDstAccessMask(vk::AccessFlagBits::eTransferWrite)
		.setOldLayout(vk::ImageLayout::eUndefined)
		.setNewLayout(vk::ImageLayout::eTransferDstOptimal)
		.setImage(dstImage)
		.setSubresourceRange({ bfrImgCp.imageSubresource.aspectMask, bfrImgCp.imageSubresource.mipLevel, 1u,
							 bfrImgCp.imageSubresource.baseArrayLayer, bfrImgCp.imageSubresource.layerCount });
	transferCmdBuffers[idx]->pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
											 {}, {}, {}, dstImMemBarrier);

	transferCmdBuffers[idx]->copyBufferToImage(srcBuffer, dstImage, vk::ImageLayout::eTransferDstOptimal, bfrImgCp);

	if (dstLayout != vk::ImageLayout{}) {
		auto dstPreImMemBarrier = vk::ImageMemoryBarrier{}
			.setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
			.setOldLayout(vk::ImageLayout::eTransferDstOptimal)
			.setNewLayout(dstLayout)
			.setImage(dstImage)
			.setSubresourceRange({ bfrImgCp.imageSubresource.aspectMask, bfrImgCp.imageSubresource.mipLevel, 1u,
							 bfrImgCp.imageSubresource.baseArrayLayer, bfrImgCp.imageSubresource.layerCount });
		transferCmdBuffers[idx]->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eBottomOfPipe,
												 {}, {}, {}, dstPreImMemBarrier);
	}

	return transferFences[idx];
}

const vk::SharedFence& ResourceCopyHandler::recordCopyCmd(vk::Image srcImage, vk::Buffer dstBuffer, vk::BufferImageCopy bfrImgCp) {
	if (!recording) startRecording();

	// Set up pipeline barriers for image transition
	auto srcImMemBarrier = vk::ImageMemoryBarrier{}
		.setSrcAccessMask(vk::AccessFlagBits::eTransferRead)
		.setOldLayout(vk::ImageLayout::eUndefined)
		.setNewLayout(vk::ImageLayout::eTransferSrcOptimal)
		.setImage(srcImage)
		.setSubresourceRange({ bfrImgCp.imageSubresource.aspectMask, bfrImgCp.imageSubresource.mipLevel, 1u,
							 bfrImgCp.imageSubresource.baseArrayLayer, bfrImgCp.imageSubresource.layerCount });
	transferCmdBuffers[idx]->pipelineBarrier(vk::PipelineStageFlagBits::eNone, vk::PipelineStageFlagBits::eTransfer,
											 {}, {}, {}, srcImMemBarrier);

	transferCmdBuffers[idx]->copyImageToBuffer(srcImage, vk::ImageLayout::eTransferSrcOptimal, dstBuffer, bfrImgCp);

	return transferFences[idx];
}

}