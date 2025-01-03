#include <accelerationstructure.h>
#include <glm/glm.hpp>

namespace vkrt {

AccelerationStructure::AccelerationStructure(vk::SharedDevice device, DeviceMemoryManager& dmm, ResourceTransferHandler& rth, Scene& scene, std::tuple<uint32_t, vk::Queue> computeQueue)
	: device(device)
	, dmm(dmm)
	, rth(rth)
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

	std::vector<std::unique_ptr<Buffer>> instanceBuffers;
	std::unique_ptr<Buffer> tlasScratchBuffer;
	buildTLAS(instanceBuffers, tlasScratchBuffer, mode);
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
	accelerationStructureGeometries.reserve(scene.geometryInfos.size());
	accelerationStructureBRIs.reserve(scene.geometryInfos.size());
	accelerationStructureBGIs.reserve(scene.geometryInfos.size());

	std::array<std::array<float, 4Ui64>, 3Ui64> transformMatrix{
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f };
	auto transformBufferCI = vk::BufferCreateInfo{}
		.setSize(sizeof(vk::TransformMatrixKHR))
		.setUsage(vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress);
	transformBuffer = std::make_unique<Buffer>(device, dmm, rth, transformBufferCI,
											   vk::ArrayProxyNoTemporaries{ sizeof(transformMatrix), (char*)&transformMatrix }, MemoryStorage::DevicePersistent);

	blasBuffers.clear();
	blasBuffers.reserve(scene.meshPool.size());
	scratchBuffers.reserve(scene.meshPool.size());
	for (auto& mesh : scene.meshPool) {
		for (int i = 0; i < mesh.vertexBuffers.size(); i++) {
			accelerationStructureGeometries.push_back(
				vk::AccelerationStructureGeometryKHR{}
				.setFlags(scene.materials[mesh.materialIndices[i]].doubleSided ? vk::GeometryFlagsKHR{} : vk::GeometryFlagBitsKHR::eOpaque)
				.setGeometryType(vk::GeometryTypeKHR::eTriangles)
				.setGeometry(vk::AccelerationStructureGeometryTrianglesDataKHR{}
							 .setVertexData(device->getBufferAddress(**mesh.vertexBuffers[i]))
							 .setVertexStride(sizeof(Vertex))
							 .setVertexFormat(vk::Format::eR32G32B32Sfloat)
							 .setMaxVertex(mesh.vertexCounts[i] - 1u)
							 .setIndexType(vk::IndexType::eUint32)
							 .setIndexData(device->getBufferAddress(**mesh.indexBuffers[i]))
							 .setTransformData(device->getBufferAddress(**transformBuffer))));
			auto accelerationStuctureBGI = vk::AccelerationStructureBuildGeometryInfoKHR{}
				.setType(vk::AccelerationStructureTypeKHR::eBottomLevel)
				.setFlags(vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace)
				.setGeometries(accelerationStructureGeometries.back());
			auto accelerationStructureBSI = device->getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, accelerationStuctureBGI, mesh.indexCounts[i] / 3u);

			auto blasBufferCI = vk::BufferCreateInfo{}
				.setSize(accelerationStructureBSI.accelerationStructureSize)
				.setUsage(vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress);
			blasBuffers.emplace_back(std::make_unique<Buffer>(device, dmm, rth, blasBufferCI,
															  nullptr, MemoryStorage::DevicePersistent));

			// With known build size we can now actually create BLAS
			auto accelerationStructureCI = vk::AccelerationStructureCreateInfoKHR{}
				.setBuffer(**blasBuffers.back())
				.setSize(accelerationStructureBSI.accelerationStructureSize)
				.setType(vk::AccelerationStructureTypeKHR::eBottomLevel);
			blas.push_back(device->createAccelerationStructureKHRUnique(accelerationStructureCI));

			scratchBuffers.emplace_back(std::make_unique<Buffer>(device, dmm, rth, vk::BufferCreateInfo{}
																 .setSize(mode == vk::BuildAccelerationStructureModeKHR::eBuild ? accelerationStructureBSI.buildScratchSize : accelerationStructureBSI.updateScratchSize)
																 .setUsage(vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress),
																 nullptr, MemoryStorage::DevicePersistent));

			accelerationStructureBGIs.push_back(accelerationStuctureBGI
												.setMode(mode)
												.setSrcAccelerationStructure(mode == vk::BuildAccelerationStructureModeKHR::eUpdate ? *blas.back() : nullptr)
												.setDstAccelerationStructure(*blas.back())
												.setScratchData(device->getBufferAddress(**scratchBuffers.back())));
			accelerationStructureBRIs.push_back(vk::AccelerationStructureBuildRangeInfoKHR{}
												.setPrimitiveCount(mesh.indexCounts[i] / 3u)
												.setPrimitiveOffset(0u)
												.setFirstVertex(0u)
												.setTransformOffset(0u));
		}
	}

	std::vector<vk::AccelerationStructureBuildRangeInfoKHR*> accelerationStructureBRIPointers;
	accelerationStructureBRIPointers.reserve(scene.geometryInfos.size());
	std::transform(accelerationStructureBRIs.begin(), accelerationStructureBRIs.end(), std::back_inserter(accelerationStructureBRIPointers),
				   [](vk::AccelerationStructureBuildRangeInfoKHR& bri) { return &bri; });
	asBuildCmdBuffer->buildAccelerationStructuresKHR(accelerationStructureBGIs, accelerationStructureBRIPointers);
}

void AccelerationStructure::buildTLAS(std::vector<std::unique_ptr<Buffer>>& instanceBuffers, std::unique_ptr<Buffer>& scratchBuffer, vk::BuildAccelerationStructureModeKHR mode) {
	std::vector<vk::AccelerationStructureInstanceKHR> instanceDataOpaque, instanceDataTransparent;
	instanceDataOpaque.reserve(scene.objectCount);
	instanceDataTransparent.reserve(scene.objectCount);

	uint32_t idx = 0u;
	for (auto& it = scene.begin(); it != scene.end(); it++) {
		const SceneObject& sceneObject = (*it);
		if (sceneObject.meshIdx == -1) continue;
		const Mesh& mesh = scene.meshPool[sceneObject.meshIdx];
		for (int i = 0; i < mesh.vertexBuffers.size(); i++) {
			std::array<std::array<float, 4Ui64>, 3Ui64> transformMatrix;
			auto affineTransform = glm::mat3x4(glm::transpose(it.transform));
			memcpy(transformMatrix.data(), &affineTransform, sizeof(transformMatrix));

			auto& instanceData = scene.materials[mesh.materialIndices[i]].baseColourFactor.a == 1.0f ? instanceDataOpaque : instanceDataTransparent;
			instanceData.push_back(vk::AccelerationStructureInstanceKHR{}
								   .setTransform(vk::TransformMatrixKHR{}.setMatrix(transformMatrix))
								   .setInstanceCustomIndex(idx)
								   .setMask(0xFF)
								   .setInstanceShaderBindingTableRecordOffset(0u)
								   .setFlags(scene.materials[scene.geometryInfos[idx].materialIdx].doubleSided ? vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable : vk::GeometryInstanceFlagBitsKHR::eTriangleFrontCounterclockwise)
								   .setAccelerationStructureReference(device->getAccelerationStructureAddressKHR(*blas[idx])));
			idx++;
		}
	}

	auto instanceBuffersOpaqueCI = vk::BufferCreateInfo{}
		.setSize(sizeof(vk::AccelerationStructureInstanceKHR) * instanceDataOpaque.size())
		.setUsage(vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress);
	auto instanceBuffersTransparentCI = vk::BufferCreateInfo{}
		.setSize(sizeof(vk::AccelerationStructureInstanceKHR) * instanceDataTransparent.size())
		.setUsage(vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress);

	instanceBuffers.emplace_back(std::make_unique<Buffer>(device, dmm, rth, instanceBuffersOpaqueCI,
														  vk::ArrayProxyNoTemporaries{ static_cast<uint32_t>(sizeof(vk::AccelerationStructureInstanceKHR) * instanceDataOpaque.size()), (char*)instanceDataOpaque.data() },
														  MemoryStorage::DeviceDynamic));
	instanceBuffers.emplace_back(std::make_unique<Buffer>(device, dmm, rth, instanceBuffersTransparentCI,
														  vk::ArrayProxyNoTemporaries{ static_cast<uint32_t>(sizeof(vk::AccelerationStructureInstanceKHR) * instanceDataTransparent.size()), (char*)instanceDataTransparent.data() },
														  MemoryStorage::DeviceDynamic));

	std::array accelerationStructureGeometries = {
		vk::AccelerationStructureGeometryKHR{}
		.setGeometryType(vk::GeometryTypeKHR::eInstances)
		.setFlags(vk::GeometryFlagBitsKHR::eOpaque)
		.setGeometry(vk::AccelerationStructureGeometryInstancesDataKHR{}
					 .setArrayOfPointers(vk::False)
					 .setData(device->getBufferAddress(**instanceBuffers[0]))),
		vk::AccelerationStructureGeometryKHR{}
		.setGeometryType(vk::GeometryTypeKHR::eInstances)
		.setGeometry(vk::AccelerationStructureGeometryInstancesDataKHR{}
					 .setArrayOfPointers(vk::False)
					 .setData(device->getBufferAddress(**instanceBuffers[1])))
	};
	auto accelerationStructureBGI = vk::AccelerationStructureBuildGeometryInfoKHR{}
		.setType(vk::AccelerationStructureTypeKHR::eTopLevel)
		.setFlags(vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace)
		.setGeometries(accelerationStructureGeometries);
	auto accelerationStructureBSI = device->getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, accelerationStructureBGI,
																				  { static_cast<uint32_t>(instanceDataOpaque.size()), static_cast<uint32_t>(instanceDataTransparent.size()) });

	if (mode == vk::BuildAccelerationStructureModeKHR::eBuild) {
		tlasBuffer = std::make_unique<Buffer>(device, dmm, rth, vk::BufferCreateInfo{}
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
	scratchBuffer = std::make_unique<Buffer>(device, dmm, rth, scratchBufferCI,
											 nullptr, MemoryStorage::DeviceDynamic);

	accelerationStructureBGI
		.setMode(mode)
		.setSrcAccelerationStructure(mode == vk::BuildAccelerationStructureModeKHR::eUpdate ? *tlas : nullptr)
		.setDstAccelerationStructure(*tlas)
		.setScratchData(device->getBufferAddress(**scratchBuffer));
	auto accelerationStructureBRIOpaque = vk::AccelerationStructureBuildRangeInfoKHR{}
		.setPrimitiveCount(instanceDataOpaque.size())
		.setPrimitiveOffset(0u)
		.setFirstVertex(0u)
		.setTransformOffset(0u);
	auto accelerationStructureBRITransparent = vk::AccelerationStructureBuildRangeInfoKHR{}
		.setPrimitiveCount(instanceDataTransparent.size())
		.setPrimitiveOffset(0u)
		.setFirstVertex(0u)
		.setTransformOffset(0u);
	std::array accelerationStructureBRIs = { accelerationStructureBRIOpaque, accelerationStructureBRITransparent };

	asBuildCmdBuffer->buildAccelerationStructuresKHR(accelerationStructureBGI, accelerationStructureBRIs.data());
}

}