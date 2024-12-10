#include <accelerationstructure.h>
#include <glm/glm.hpp>

namespace vkrt {

AccelerationStructure::AccelerationStructure(vk::SharedDevice device, DeviceMemoryManager& dmm, ResourceCopyHandler& rch, Scene& scene, std::tuple<uint32_t, vk::Queue> computeQueue)
	: device(device)
	, dmm(dmm)
	, rch(rch)
	, scene(scene)
	, computeQueue(computeQueue)
	, commandPool(device->createCommandPoolUnique(vk::CommandPoolCreateInfo{}
												  .setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer)
												  .setQueueFamilyIndex(std::get<uint32_t>(computeQueue))))
	, asBuildCmdBuffer(std::move(device->allocateCommandBuffersUnique(vk::CommandBufferAllocateInfo{}
																	  .setCommandPool(*commandPool)
																	  .setCommandBufferCount(1u)
																	  .setLevel(vk::CommandBufferLevel::ePrimary)).front()))
	, buildFinishedFence(vk::SharedFence(device->createFence(vk::FenceCreateInfo{}.setFlags(vk::FenceCreateFlagBits::eSignaled)), device))
{
	blas.reserve(scene.meshPool.size());
	blasBuffers.reserve(scene.meshPool.size());
	build(vk::BuildAccelerationStructureModeKHR::eBuild);
}

vk::SharedFence AccelerationStructure::rebuild(vk::ArrayProxyNoTemporaries<vk::SharedSemaphore> waitSemaphores, vk::ArrayProxyNoTemporaries<vk::SharedSemaphore> signalSemaphores) {
	return build(vk::BuildAccelerationStructureModeKHR::eBuild, waitSemaphores, signalSemaphores);
}

vk::SharedFence AccelerationStructure::update(vk::ArrayProxyNoTemporaries<vk::SharedSemaphore> waitSemaphores, vk::ArrayProxyNoTemporaries<vk::SharedSemaphore> signalSemaphores) {
	return build(vk::BuildAccelerationStructureModeKHR::eUpdate, waitSemaphores, signalSemaphores);
}

vk::SharedFence AccelerationStructure::build(vk::BuildAccelerationStructureModeKHR mode, vk::ArrayProxyNoTemporaries<vk::SharedSemaphore> waitSemaphores, vk::ArrayProxyNoTemporaries<vk::SharedSemaphore> signalSemaphores) {
	device->waitForFences(*buildFinishedFence, vk::True, std::numeric_limits<uint64_t>::max());
	asBuildCmdBuffer->reset();
	asBuildCmdBuffer->begin(vk::CommandBufferBeginInfo{}.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

	std::vector<std::unique_ptr<Buffer>> blasScratchBuffers;
	buildBLAS(blasScratchBuffers, mode);

	// Insert pipeline barrier betwee BLAS and TLAS build
	auto memBarrier = vk::MemoryBarrier{}
		.setSrcAccessMask(vk::AccessFlagBits::eAccelerationStructureWriteKHR)
		.setDstAccessMask(vk::AccessFlagBits::eAccelerationStructureReadKHR);
	std::vector<vk::BufferMemoryBarrier> blasMemBarriers;
	blasMemBarriers.reserve(blasBuffers.size());
	for (auto& b : blasBuffers) {
		blasMemBarriers.push_back(vk::BufferMemoryBarrier{}
								  .setBuffer(**b)
								  .setSrcAccessMask(vk::AccessFlagBits::eAccelerationStructureWriteKHR)
								  .setDstAccessMask(vk::AccessFlagBits::eAccelerationStructureReadKHR)
								  .setOffset(0u)
								  .setSize(vk::WholeSize));
	}
	asBuildCmdBuffer->pipelineBarrier(vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR, vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
									  {}, memBarrier, blasMemBarriers, {});

	std::unique_ptr<Buffer> instancesBuffer, tlasScratchBuffer;
	buildTLAS(instancesBuffer, tlasScratchBuffer, mode);
	asBuildCmdBuffer->end();

	// Submit build commands
	std::vector<vk::PipelineStageFlags> submitWaitDstStageMask(waitSemaphores.size());
	std::vector<vk::Semaphore> submitWaitSemaphores(waitSemaphores.size());
	std::vector<vk::Semaphore> submitSignalSemaphores(signalSemaphores.size());
	std::fill(submitWaitDstStageMask.begin(), submitWaitDstStageMask.end(), vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR);
	std::transform(waitSemaphores.begin(), waitSemaphores.end(), submitWaitSemaphores.begin(),
				   [](const vk::SharedSemaphore& ws) { return *ws; });
	std::transform(signalSemaphores.begin(), signalSemaphores.end(), submitSignalSemaphores.begin(),
				   [](const vk::SharedSemaphore& ss) { return *ss; });
	auto submitInfo = vk::SubmitInfo{}
		.setCommandBuffers(*asBuildCmdBuffer)
		.setWaitDstStageMask(submitWaitDstStageMask)
		.setWaitSemaphores(submitWaitSemaphores)
		.setSignalSemaphores(submitSignalSemaphores);
	device->resetFences(*buildFinishedFence);
	std::get<vk::Queue>(computeQueue).submit(submitInfo, *buildFinishedFence);
	CHECK_VULKAN_RESULT(device->waitForFences(*buildFinishedFence, vk::True, std::numeric_limits<uint64_t>::max()));

	return buildFinishedFence; // return value currently useless as I'm already waiting for fence
}

// TODO: compaction, build in chunks to avoid stalling pipeline?, shared scratch buffer
void AccelerationStructure::buildBLAS(std::vector<std::unique_ptr<Buffer>>& scratchBuffers, vk::BuildAccelerationStructureModeKHR mode) {
	std::vector<vk::AccelerationStructureGeometryKHR> accelerationStructureGeometries;
	std::vector<vk::AccelerationStructureBuildGeometryInfoKHR> accelerationStructureBGIs;
	std::vector<vk::AccelerationStructureBuildRangeInfoKHR> accelerationStructureBRIs;
	accelerationStructureGeometries.reserve(scene.meshPool.size());
	accelerationStructureBRIs.reserve(scene.meshPool.size());
	accelerationStructureBGIs.reserve(scene.meshPool.size());

	std::array<std::array<float, 4Ui64>, 3Ui64> transformMatrix{
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f };
	auto transformBufferCI = vk::BufferCreateInfo{}
		.setSize(sizeof(vk::TransformMatrixKHR))
		.setUsage(vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress);
	transformBuffer = std::make_unique<Buffer>(device, dmm, rch, transformBufferCI,
											   vk::ArrayProxyNoTemporaries{ sizeof(transformMatrix), (char*)&transformMatrix }, MemoryStorage::DevicePersistent);

	blasBuffers.clear();
	blasBuffers.reserve(scene.meshPool.size());
	scratchBuffers.reserve(scene.meshPool.size());
	for (auto& mesh : scene.meshPool) {
		accelerationStructureGeometries.push_back(
			vk::AccelerationStructureGeometryKHR{}
			.setFlags(mesh.transparent ? vk::GeometryFlagsKHR{} : vk::GeometryFlagBitsKHR::eOpaque)
			.setGeometryType(vk::GeometryTypeKHR::eTriangles)
			.setGeometry(vk::AccelerationStructureGeometryTrianglesDataKHR{}
						 .setVertexData(device->getBufferAddress(**mesh.vertices))
						 .setVertexStride(sizeof(glm::vec3))
						 .setVertexFormat(vk::Format::eR32G32B32Sfloat)
						 .setMaxVertex(mesh.nVertices - 1u)
						 .setIndexType(vk::IndexType::eUint32)
						 .setIndexData(device->getBufferAddress(**mesh.indices))
						 .setTransformData(device->getBufferAddress(**transformBuffer))));
		auto accelerationStuctureBGI = vk::AccelerationStructureBuildGeometryInfoKHR{}
			.setType(vk::AccelerationStructureTypeKHR::eBottomLevel)
			.setFlags(vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace)
			.setGeometries(accelerationStructureGeometries.back());
		auto accelerationStructureBSI = device->getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, accelerationStuctureBGI, mesh.nIndices / 3u);

		auto blasBufferCI = vk::BufferCreateInfo{}
			.setSize(accelerationStructureBSI.accelerationStructureSize)
			.setUsage(vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress);
		blasBuffers.emplace_back(std::make_unique<Buffer>(device, dmm, rch, blasBufferCI,
														  nullptr, MemoryStorage::DevicePersistent));

		// With known build size we can now actually create BLAS
		auto accelerationStructureCI = vk::AccelerationStructureCreateInfoKHR{}
			.setBuffer(**blasBuffers.back())
			.setSize(accelerationStructureBSI.accelerationStructureSize)
			.setType(vk::AccelerationStructureTypeKHR::eBottomLevel);
		blas.push_back(device->createAccelerationStructureKHRUnique(accelerationStructureCI));

		scratchBuffers.emplace_back(std::make_unique<Buffer>(device, dmm, rch, vk::BufferCreateInfo{}
															 .setSize(mode == vk::BuildAccelerationStructureModeKHR::eBuild ? accelerationStructureBSI.buildScratchSize : accelerationStructureBSI.updateScratchSize)
															 .setUsage(vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress),
															 nullptr, MemoryStorage::DevicePersistent));

		accelerationStructureBGIs.push_back(accelerationStuctureBGI
											.setMode(mode)
											.setSrcAccelerationStructure(mode == vk::BuildAccelerationStructureModeKHR::eUpdate ? *blas.back() : nullptr)
											.setDstAccelerationStructure(*blas.back())
											.setScratchData(device->getBufferAddress(**scratchBuffers.back())));
		accelerationStructureBRIs.push_back(vk::AccelerationStructureBuildRangeInfoKHR{}
											.setPrimitiveCount(mesh.nIndices / 3u)
											.setPrimitiveOffset(0u)
											.setFirstVertex(0u)
											.setTransformOffset(0u));
	}

	auto accelerationStructureBRIPointers = std::vector<vk::AccelerationStructureBuildRangeInfoKHR*>(scene.meshPool.size());
	std::transform(accelerationStructureBRIs.begin(), accelerationStructureBRIs.end(), accelerationStructureBRIPointers.begin(),
				   [](vk::AccelerationStructureBuildRangeInfoKHR& bri) { return &bri; });
	asBuildCmdBuffer->buildAccelerationStructuresKHR(accelerationStructureBGIs, accelerationStructureBRIPointers);
}

void AccelerationStructure::buildTLAS(std::unique_ptr<Buffer>& instancesBuffer, std::unique_ptr<Buffer>& scratchBuffer, vk::BuildAccelerationStructureModeKHR mode) {
	std::vector<vk::AccelerationStructureInstanceKHR> instanceData;
	instanceData.reserve(scene.objectCount);

	uint32_t idx = 0;
	for (auto& it = scene.begin(); it != scene.end(); it++) {
		const auto& sceneObject = (*it);
		for (auto meshIdx : sceneObject.meshIndices) {
			std::array<std::array<float, 4Ui64>, 3Ui64> transformMatrix;
			auto affineTransform = glm::mat3x4(glm::transpose(it.transform));
			memcpy(transformMatrix.data(), &affineTransform, sizeof(transformMatrix));
			instanceData.push_back(vk::AccelerationStructureInstanceKHR{}
								   .setTransform(vk::TransformMatrixKHR{}.setMatrix(transformMatrix))
								   .setInstanceCustomIndex(meshIdx)
								   .setMask(0xFF)
								   .setInstanceShaderBindingTableRecordOffset(0u)
								   .setFlags(vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable)
								   .setAccelerationStructureReference(device->getAccelerationStructureAddressKHR(*blas[meshIdx])));
			idx++;
		}
	}
	auto instancesBufferCI = vk::BufferCreateInfo{}
		.setSize(sizeof(vk::AccelerationStructureInstanceKHR) * instanceData.size())
		.setUsage(vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress);
	instancesBuffer = std::make_unique<Buffer>(device, dmm, rch, instancesBufferCI,
											   vk::ArrayProxyNoTemporaries{ static_cast<uint32_t>(sizeof(vk::AccelerationStructureInstanceKHR) * instanceData.size()), (char*)instanceData.data() },
											   MemoryStorage::DeviceDynamic);

	auto accelerationStructureGeometry = vk::AccelerationStructureGeometryKHR{}
		.setGeometryType(vk::GeometryTypeKHR::eInstances)
		.setFlags(vk::GeometryFlagBitsKHR::eOpaque)
		.setGeometry(vk::AccelerationStructureGeometryInstancesDataKHR{}
					 .setArrayOfPointers(vk::False)
					 .setData(device->getBufferAddress(**instancesBuffer)));
	auto accelerationStructureBGI = vk::AccelerationStructureBuildGeometryInfoKHR{}
		.setType(vk::AccelerationStructureTypeKHR::eTopLevel)
		.setFlags(vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace)
		.setGeometries(accelerationStructureGeometry);
	auto accelerationStructureBSI = device->getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, accelerationStructureBGI, instanceData.size());

	if (mode == vk::BuildAccelerationStructureModeKHR::eBuild) {
		tlasBuffer = std::make_unique<Buffer>(device, dmm, rch, vk::BufferCreateInfo{}
											  .setSize(accelerationStructureBSI.accelerationStructureSize)
											  .setUsage(vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress),
											  nullptr, MemoryStorage::DevicePersistent);
		auto accelerationStructureCI = vk::AccelerationStructureCreateInfoKHR{}
			.setBuffer(**tlasBuffer)
			.setSize(accelerationStructureBSI.accelerationStructureSize)
			.setType(vk::AccelerationStructureTypeKHR::eTopLevel);
		tlas = device->createAccelerationStructureKHRUnique(accelerationStructureCI);
	}

	auto scratchBufferCI = vk::BufferCreateInfo{}
		.setSize(mode == vk::BuildAccelerationStructureModeKHR::eBuild ? accelerationStructureBSI.buildScratchSize : accelerationStructureBSI.updateScratchSize)
		.setUsage(vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress);
	scratchBuffer = std::make_unique<Buffer>(device, dmm, rch, scratchBufferCI,
											 nullptr, MemoryStorage::DeviceDynamic);

	accelerationStructureBGI
		.setMode(mode)
		.setSrcAccelerationStructure(mode == vk::BuildAccelerationStructureModeKHR::eUpdate ? *tlas : nullptr)
		.setDstAccelerationStructure(*tlas)
		.setScratchData(device->getBufferAddress(**scratchBuffer));
	auto accelerationStructureBRI = vk::AccelerationStructureBuildRangeInfoKHR{}
		.setPrimitiveCount(instanceData.size())
		.setPrimitiveOffset(0u)
		.setFirstVertex(0u)
		.setTransformOffset(0u);

	asBuildCmdBuffer->buildAccelerationStructuresKHR(accelerationStructureBGI, &accelerationStructureBRI);
}

}