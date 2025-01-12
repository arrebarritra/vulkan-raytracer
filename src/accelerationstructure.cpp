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

	std::unique_ptr<Buffer> instanceBuffer, tlasScratchBuffer;
	buildTLAS(instanceBuffer, tlasScratchBuffer, mode);
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

	blasBuffers.clear();
	blasBuffers.reserve(scene.meshPool.size());
	scratchBuffers.reserve(scene.meshPool.size());
	for (auto& mesh : scene.meshPool) {
		for (int i = 0; i < mesh.primitiveCount; i++) {
			accelerationStructureGeometries.push_back(
				vk::AccelerationStructureGeometryKHR{}
				.setFlags(scene.materials[mesh.materialIndices[i]].alphaMode != 0 ? vk::GeometryFlagsKHR{} : vk::GeometryFlagBitsKHR::eOpaque)
				.setGeometryType(vk::GeometryTypeKHR::eTriangles)
				.setGeometry(vk::AccelerationStructureGeometryTrianglesDataKHR{}
							 .setVertexData(device->getBufferAddress(**mesh.vertexBuffers[i]))
							 .setVertexStride(sizeof(Vertex))
							 .setVertexFormat(vk::Format::eR32G32B32Sfloat)
							 .setMaxVertex(mesh.vertexCounts[i] - 1u)
							 .setIndexType(vk::IndexType::eUint32)
							 .setIndexData(device->getBufferAddress(**mesh.indexBuffers[i]))));
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

void AccelerationStructure::buildTLAS(std::unique_ptr<Buffer>& instanceBuffer, std::unique_ptr<Buffer>& scratchBuffer, vk::BuildAccelerationStructureModeKHR mode) {
	std::vector<vk::AccelerationStructureInstanceKHR> instanceData;
	instanceData.reserve(scene.objectCount);

	for (auto& it = scene.begin(); it != scene.end(); it++) {
		const SceneObject& sceneObject = (*it);
		if (sceneObject.meshIdx == -1) continue;
		const Mesh& mesh = scene.meshPool[sceneObject.meshIdx];
		for (int i = 0; i < mesh.primitiveCount; i++) {
			std::array<std::array<float, 4Ui64>, 3Ui64> transformMatrix;
			auto affineTransform = glm::mat3x4(glm::transpose(it.transform));
			memcpy(transformMatrix.data(), &affineTransform, sizeof(transformMatrix));

			const auto& mat = scene.materials[scene.geometryInfos[mesh.primitiveOffset + i].materialIdx];
			uint32_t objectMask = 1u;
			if (mat.emissiveFactor == glm::vec3(0.0))
				objectMask |= 1u << 1;
			instanceData.push_back(vk::AccelerationStructureInstanceKHR{}
								   .setTransform(vk::TransformMatrixKHR{}.setMatrix(transformMatrix))
								   .setInstanceCustomIndex(mesh.primitiveOffset + i)
								   .setMask(objectMask)
								   .setInstanceShaderBindingTableRecordOffset(0u)
								   .setFlags(mat.doubleSided ? vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable : vk::GeometryInstanceFlagBitsKHR{})
								   .setAccelerationStructureReference(device->getAccelerationStructureAddressKHR(*blas[mesh.primitiveOffset + i])));
		}
	}

	auto instanceBuffersCI = vk::BufferCreateInfo{}
		.setSize(sizeof(vk::AccelerationStructureInstanceKHR) * instanceData.size())
		.setUsage(vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress);
	instanceBuffer = std::make_unique<Buffer>(device, dmm, rth, instanceBuffersCI,
											  vk::ArrayProxyNoTemporaries{ static_cast<uint32_t>(sizeof(vk::AccelerationStructureInstanceKHR) * instanceData.size()), (char*)instanceData.data() },
											  MemoryStorage::DeviceDynamic);

	auto accelerationStructureGeometry = vk::AccelerationStructureGeometryKHR{}
		.setGeometryType(vk::GeometryTypeKHR::eInstances)
		.setFlags(vk::GeometryFlagBitsKHR::eOpaque)
		.setGeometry(vk::AccelerationStructureGeometryInstancesDataKHR{}
					 .setArrayOfPointers(vk::False)
					 .setData(device->getBufferAddress(**instanceBuffer)));
	auto accelerationStructureBGI = vk::AccelerationStructureBuildGeometryInfoKHR{}
		.setType(vk::AccelerationStructureTypeKHR::eTopLevel)
		.setFlags(vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace)
		.setGeometries(accelerationStructureGeometry);
	auto accelerationStructureBSI = device->getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, accelerationStructureBGI, instanceData.size());

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
	auto accelerationStructureBRI = vk::AccelerationStructureBuildRangeInfoKHR{}
		.setPrimitiveCount(instanceData.size())
		.setPrimitiveOffset(0u)
		.setFirstVertex(0u)
		.setTransformOffset(0u);

	asBuildCmdBuffer->buildAccelerationStructuresKHR(accelerationStructureBGI, &accelerationStructureBRI);
}

}