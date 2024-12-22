#include <resourcetransferhandler.h>

namespace vkrt {
ResourceTransferHandler::ResourceTransferHandler(vk::SharedDevice device, std::tuple<uint32_t, vk::Queue> transferQueue)
	: device(device), transferQueue(transferQueue)
{
	auto cmdPoolCI = vk::CommandPoolCreateInfo{}
		.setQueueFamilyIndex(std::get<uint32_t>(transferQueue))
		.setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
	commandPool = device->createCommandPoolUnique(cmdPoolCI);

	auto buffersTmp = device->allocateCommandBuffersUnique(vk::CommandBufferAllocateInfo{}
														   .setCommandPool(*commandPool)
														   .setCommandBufferCount(1u)
														   .setLevel(vk::CommandBufferLevel::ePrimary));
}

const void ResourceTransferHandler::submit(vk::CommandBuffer cmdBuffer, SyncInfo si) {
	freeCompletedTransfers();
	// Create submit info
	std::vector<vk::PipelineStageFlags> submitWaitDstStageMask(si.waitSemaphores.size());
	std::vector<vk::Semaphore> submitWaitSemaphores(si.waitSemaphores.size());
	std::vector<vk::Semaphore> submitSignalSemaphores(si.signalSemaphores.size());
	std::fill(submitWaitDstStageMask.begin(), submitWaitDstStageMask.end(), vk::PipelineStageFlagBits::eTransfer);
	std::transform(si.waitSemaphores.begin(), si.waitSemaphores.end(), submitWaitSemaphores.begin(),
				   [](const vk::SharedSemaphore& ws) { return *ws; });
	std::transform(si.signalSemaphores.begin(), si.signalSemaphores.end(), submitSignalSemaphores.begin(),
				   [](const vk::SharedSemaphore& ss) { return *ss; });

	auto submitInfo = vk::SubmitInfo{}
		.setCommandBuffers(cmdBuffer)
		.setWaitDstStageMask(submitWaitDstStageMask)
		.setWaitSemaphores(submitWaitSemaphores)
		.setSignalSemaphores(submitSignalSemaphores);
	std::get<vk::Queue>(transferQueue).submit(submitInfo, *si.fence);
}

const void ResourceTransferHandler::copy(vk::Buffer srcBuffer, vk::Buffer dstBuffer, vk::BufferCopy bfrCp, SyncInfo si, std::unique_ptr<ManagedResource> stagedResource) {
	auto cmdBuffer = std::move(device->allocateCommandBuffersUnique(vk::CommandBufferAllocateInfo{}
																	.setCommandPool(*commandPool)
																	.setCommandBufferCount(1u)
																	.setLevel(vk::CommandBufferLevel::ePrimary))[0]);
	cmdBuffer->copyBuffer(srcBuffer, dstBuffer, bfrCp);
	submit(*cmdBuffer, si);
	pendingTransfers.emplace(*si.fence, std::make_tuple(std::move(cmdBuffer), si, std::move(stagedResource)));
}

const void ResourceTransferHandler::copy(vk::Image srcImage, vk::Image dstImage, vk::ImageCopy imgCp, vk::ImageLayout dstLayout, SyncInfo si, std::unique_ptr<ManagedResource> stagedResource) {
	auto cmdBuffer = std::move(device->allocateCommandBuffersUnique(vk::CommandBufferAllocateInfo{}
																	.setCommandPool(*commandPool)
																	.setCommandBufferCount(1u)
																	.setLevel(vk::CommandBufferLevel::ePrimary))[0]);

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
	cmdBuffer->pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
							   {}, {}, {}, preImMemBarriers);


	cmdBuffer->copyImage(srcImage, vk::ImageLayout::eTransferSrcOptimal, dstImage, vk::ImageLayout::eTransferDstOptimal, imgCp);

	if (dstLayout != vk::ImageLayout{}) {
		auto dstPostImMemBarrier = vk::ImageMemoryBarrier{}
			.setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
			.setOldLayout(vk::ImageLayout::eTransferDstOptimal)
			.setNewLayout(dstLayout)
			.setImage(dstImage)
			.setSubresourceRange({ imgCp.srcSubresource.aspectMask, imgCp.srcSubresource.mipLevel, 1u,
								 imgCp.srcSubresource.baseArrayLayer, imgCp.srcSubresource.layerCount });
		cmdBuffer->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eBottomOfPipe,
								   {}, {}, {}, dstPostImMemBarrier);
	}

	submit(*cmdBuffer, si);
	pendingTransfers.emplace(*si.fence, std::make_tuple(std::move(cmdBuffer), si, std::move(stagedResource)));
}

const void ResourceTransferHandler::copy(vk::Buffer srcBuffer, vk::Image dstImage, vk::BufferImageCopy bfrImgCp, vk::ImageLayout dstLayout, SyncInfo si, std::unique_ptr<ManagedResource> stagedResource) {
	auto cmdBuffer = std::move(device->allocateCommandBuffersUnique(vk::CommandBufferAllocateInfo{}
																	.setCommandPool(*commandPool)
																	.setCommandBufferCount(1u)
																	.setLevel(vk::CommandBufferLevel::ePrimary))[0]);

	// Set up pipeline barriers for image transition
	auto dstImMemBarrier = vk::ImageMemoryBarrier{}
		.setDstAccessMask(vk::AccessFlagBits::eTransferWrite)
		.setOldLayout(vk::ImageLayout::eUndefined)
		.setNewLayout(vk::ImageLayout::eTransferDstOptimal)
		.setImage(dstImage)
		.setSubresourceRange({ bfrImgCp.imageSubresource.aspectMask, bfrImgCp.imageSubresource.mipLevel, 1u,
							 bfrImgCp.imageSubresource.baseArrayLayer, bfrImgCp.imageSubresource.layerCount });
	cmdBuffer->pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
							   {}, {}, {}, dstImMemBarrier);

	cmdBuffer->copyBufferToImage(srcBuffer, dstImage, vk::ImageLayout::eTransferDstOptimal, bfrImgCp);

	if (dstLayout != vk::ImageLayout{}) {
		auto dstPreImMemBarrier = vk::ImageMemoryBarrier{}
			.setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
			.setOldLayout(vk::ImageLayout::eTransferDstOptimal)
			.setNewLayout(dstLayout)
			.setImage(dstImage)
			.setSubresourceRange({ bfrImgCp.imageSubresource.aspectMask, bfrImgCp.imageSubresource.mipLevel, 1u,
							 bfrImgCp.imageSubresource.baseArrayLayer, bfrImgCp.imageSubresource.layerCount });
		cmdBuffer->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eBottomOfPipe,
								   {}, {}, {}, dstPreImMemBarrier);
	}

	submit(*cmdBuffer, si);
	pendingTransfers.emplace(*si.fence, std::make_tuple(std::move(cmdBuffer), si, std::move(stagedResource)));
}

const void ResourceTransferHandler::copy(vk::Image srcImage, vk::Buffer dstBuffer, vk::BufferImageCopy bfrImgCp, SyncInfo si, std::unique_ptr<ManagedResource> stagedResource) {
	auto cmdBuffer = std::move(device->allocateCommandBuffersUnique(vk::CommandBufferAllocateInfo{}
																	.setCommandPool(*commandPool)
																	.setCommandBufferCount(1u)
																	.setLevel(vk::CommandBufferLevel::ePrimary))[0]);
	// Set up pipeline barriers for image transition
	auto srcImMemBarrier = vk::ImageMemoryBarrier{}
		.setSrcAccessMask(vk::AccessFlagBits::eTransferRead)
		.setOldLayout(vk::ImageLayout::eUndefined)
		.setNewLayout(vk::ImageLayout::eTransferSrcOptimal)
		.setImage(srcImage)
		.setSubresourceRange({ bfrImgCp.imageSubresource.aspectMask, bfrImgCp.imageSubresource.mipLevel, 1u,
							 bfrImgCp.imageSubresource.baseArrayLayer, bfrImgCp.imageSubresource.layerCount });
	cmdBuffer->pipelineBarrier(vk::PipelineStageFlagBits::eNone, vk::PipelineStageFlagBits::eTransfer,
							   {}, {}, {}, srcImMemBarrier);

	cmdBuffer->copyImageToBuffer(srcImage, vk::ImageLayout::eTransferSrcOptimal, dstBuffer, bfrImgCp);

	submit(*cmdBuffer, si);
	pendingTransfers.emplace(*si.fence, std::make_tuple(std::move(cmdBuffer), si, std::move(stagedResource)));
}

const void ResourceTransferHandler::freeCompletedTransfers() {
	for (auto it = pendingTransfers.begin(); it != pendingTransfers.end();) {
		if (device->getFenceStatus((*it).first) == vk::Result::eSuccess)
			it = pendingTransfers.erase(it);
		else
			it++;
	}
}

const void ResourceTransferHandler::flushPendingTransfers(vk::ArrayProxyNoTemporaries<vk::SharedFence> fences) {
	std::vector<vk::Fence> fenceHandles;
	fenceHandles.reserve(pendingTransfers.size());
	std::transform(fences.begin(), fences.end(), std::back_inserter(fenceHandles),
				   [](const vk::SharedFence& fence) { return *fence; });
	CHECK_VULKAN_RESULT(device->waitForFences(fenceHandles, vk::True, std::numeric_limits<uint64_t>::max()));

	for (const auto& fence : fences)
		pendingTransfers.erase(*fence);
}

const void ResourceTransferHandler::flushPendingTransfers() {
	std::vector<vk::Fence> fences;
	fences.reserve(pendingTransfers.size());
	std::transform(pendingTransfers.begin(), pendingTransfers.end(), std::back_inserter(fences),
				   [](const auto& transferInfo) { return transferInfo.first; });
	CHECK_VULKAN_RESULT(device->waitForFences(fences, vk::True, std::numeric_limits<uint64_t>::max()));
	pendingTransfers.clear();
}

}