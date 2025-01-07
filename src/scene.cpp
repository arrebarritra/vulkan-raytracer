#include <scene.h>

#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace vkrt {

SceneObject::SceneObject(SceneObject* parent, glm::mat4& transform, int meshIdx)
	: transform(transform), parent(parent), meshIdx(meshIdx), depth(parent ? parent->depth + 1u : 0u) {}

Scene::Scene(vk::SharedDevice device, DeviceMemoryManager& dmm, ResourceTransferHandler& rth)
	: device(device), dmm(dmm), rth(rth), root(nullptr, glm::mat4(1.0f), -1), objectCount(0u), maxDepth(1u) {}

SceneObject& Scene::addObject(SceneObject* parent, glm::mat4& transform, int meshIdx) {
	objectCount++;
	auto& so = parent->children.emplace_back(parent ? parent : &root, transform, meshIdx);
	maxDepth = std::max(maxDepth, so.depth);
	return so;
}

void Scene::loadModel(SceneObject* parent, glm::mat4& transform, std::filesystem::path path) {
	LOG_INFO("Loading model %s", path.filename().string().c_str());
	tinygltf::Model model;
	tinygltf::TinyGLTF context;
	std::string err, warn;

	if (!context.LoadASCIIFromFile(&model, &err, &warn, path.string())) {
		LOG_ERROR("TinyGLTF error: %s", err.c_str());
		throw std::runtime_error(err);
	}

	// Load meshes
	LOG_INFO("Loading %d meshes", model.meshes.size());
	uint32_t baseMeshOffset = meshPool.size();
	uint32_t baseMaterialOffset = materials.size();
	meshPool.reserve(meshPool.size() + model.meshes.size());
	geometryInfos.reserve(geometryInfos.size() + model.meshes.size());
	materials.reserve(materials.size() + model.materials.size());
	for (const auto& gltfMesh : model.meshes) {
		std::vector<std::vector<Vertex>> primitiveVertices;
		std::vector<std::vector<Index>> primitiveIndices;
		std::vector<uint32_t> materialIndices;
		primitiveVertices.reserve(gltfMesh.primitives.size());
		primitiveIndices.reserve(gltfMesh.primitives.size());
		materialIndices.reserve(gltfMesh.primitives.size());
		for (const auto& gltfPrimitive : gltfMesh.primitives) {
			// From https://github.com/SaschaWillems/Vulkan/blob/master/examples/gltfscenerendering/gltfscenerendering.cpp#L352
			// Vertices
			{
				const float* positionBuffer = nullptr;
				const float* normalsBuffer = nullptr;
				const float* texCoordsBuffer0 = nullptr;
				const float* texCoordsBuffer1 = nullptr;
				const float* tangentsBuffer = nullptr;
				size_t vertexCount = 0;

				// Get buffer data for vertex normals
				if (gltfPrimitive.attributes.find("POSITION") != gltfPrimitive.attributes.end()) {
					const tinygltf::Accessor& accessor = model.accessors[gltfPrimitive.attributes.find("POSITION")->second];
					const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
					positionBuffer = reinterpret_cast<const float*>(&(model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
					vertexCount = accessor.count;
				}
				// Get buffer data for vertex normals
				if (gltfPrimitive.attributes.find("NORMAL") != gltfPrimitive.attributes.end()) {
					const tinygltf::Accessor& accessor = model.accessors[gltfPrimitive.attributes.find("NORMAL")->second];
					const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
					normalsBuffer = reinterpret_cast<const float*>(&(model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
				}
				// Get buffer data for vertex texture coordinates
				// glTF supports multiple sets, we only load the first one
				if (gltfPrimitive.attributes.find("TEXCOORD_0") != gltfPrimitive.attributes.end()) {
					const tinygltf::Accessor& accessor = model.accessors[gltfPrimitive.attributes.find("TEXCOORD_0")->second];
					const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
					texCoordsBuffer0 = reinterpret_cast<const float*>(&(model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
				}
				if (gltfPrimitive.attributes.find("TEXCOORD_1") != gltfPrimitive.attributes.end()) {
					const tinygltf::Accessor& accessor = model.accessors[gltfPrimitive.attributes.find("TEXCOORD_1")->second];
					const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
					texCoordsBuffer1 = reinterpret_cast<const float*>(&(model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
				}
				// POI: This sample uses normal mapping, so we also need to load the tangents from the glTF file
				if (gltfPrimitive.attributes.find("TANGENT") != gltfPrimitive.attributes.end()) {
					const tinygltf::Accessor& accessor = model.accessors[gltfPrimitive.attributes.find("TANGENT")->second];
					const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
					tangentsBuffer = reinterpret_cast<const float*>(&(model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
				}

				// Append data to model's vertex buffer
				std::vector<Vertex> vertices;
				for (size_t v = 0; v < vertexCount; v++) {
					Vertex vertex;
					vertex.position = glm::make_vec3(&positionBuffer[v * 3]);
					vertex.normal = glm::normalize(glm::vec3(normalsBuffer ? glm::make_vec3(&normalsBuffer[v * 3]) : glm::vec3(0.0f)));
					vertex.uv0 = texCoordsBuffer0 ? glm::make_vec2(&texCoordsBuffer0[v * 2]) : glm::vec3(0.0f);
					vertex.uv1 = texCoordsBuffer1 ? glm::make_vec2(&texCoordsBuffer1[v * 2]) : glm::vec3(0.0f);
					vertex.tangent = tangentsBuffer ? glm::make_vec4(&tangentsBuffer[v * 3]) : glm::vec4(0.0f);
					vertices.push_back(vertex);
				}
				primitiveVertices.push_back(std::move(vertices));
			}
			// Indices
			{
				const tinygltf::Accessor& accessor = model.accessors[gltfPrimitive.indices];
				const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
				const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];

				std::vector<Index> indices;
				// glTF supports different component types of indices
				switch (accessor.componentType) {
					case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT: {
						const uint32_t* buf = reinterpret_cast<const uint32_t*>(&buffer.data[accessor.byteOffset + bufferView.byteOffset]);
						for (size_t index = 0; index < accessor.count; index++)	indices.push_back(buf[index]);
						break;
					}
					case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT: {
						const uint16_t* buf = reinterpret_cast<const uint16_t*>(&buffer.data[accessor.byteOffset + bufferView.byteOffset]);
						for (size_t index = 0; index < accessor.count; index++)	indices.push_back(buf[index]);
						break;
					}
					case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE: {
						const uint8_t* buf = reinterpret_cast<const uint8_t*>(&buffer.data[accessor.byteOffset + bufferView.byteOffset]);
						for (size_t index = 0; index < accessor.count; index++)	indices.push_back(buf[index]);
						break;
					}
					default:
						LOG_ERROR("Index component type %s not supported!", accessor.componentType);
						return;
				}
				primitiveIndices.push_back(std::move(indices));
			}
			materialIndices.push_back(baseMaterialOffset + gltfPrimitive.material);
		}
		meshPool.emplace_back(device, dmm, rth, primitiveVertices, primitiveIndices, materialIndices);
		for (int i = 0; i < gltfMesh.primitives.size(); i++)
			geometryInfos.emplace_back(device->getBufferAddress(**meshPool.back().vertexBuffers[i]), device->getBufferAddress(**meshPool.back().indexBuffers[i]), meshPool.back().materialIndices[i]);
	}

	LOG_INFO("Creating geometry info buffer with %d geometries", geometryInfos.size());
	auto geometryInfoBufferCI = vk::BufferCreateInfo{}
		.setSize(geometryInfos.size() * sizeof(GeometryInfo))
		.setUsage(vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eStorageBuffer);
	geometryInfoBuffer = std::make_unique<Buffer>(device, dmm, rth, geometryInfoBufferCI, vk::ArrayProxyNoTemporaries{ static_cast<uint32_t>(geometryInfoBufferCI.size), (char*)geometryInfos.data() }, MemoryStorage::DevicePersistent);

	// Load materials and associated textures
	LOG_INFO("Loading %d materials", model.materials.size());
	for (const auto& gltfMaterial : model.materials) {
		Material material;
		material.baseColourFactor = glm::make_vec4(gltfMaterial.pbrMetallicRoughness.baseColorFactor.data());
		material.emissiveFactor = glm::make_vec3(gltfMaterial.emissiveFactor.data());
		if (gltfMaterial.pbrMetallicRoughness.baseColorTexture.index != -1)
			material.baseColourTexIdx = baseMaterialOffset + gltfMaterial.pbrMetallicRoughness.baseColorTexture.index;
		if (gltfMaterial.emissiveTexture.index != -1)
			material.baseColourTexIdx = baseMaterialOffset + gltfMaterial.emissiveTexture.index;

		material.metallicFactor = gltfMaterial.pbrMetallicRoughness.metallicFactor;
		material.roughnessFactor = gltfMaterial.pbrMetallicRoughness.roughnessFactor;
		if (gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index != -1)
			material.metallicRoughnessTexIdx = baseMaterialOffset + gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index;
		if (gltfMaterial.normalTexture.index != -1)
			material.normalTexIdx = baseMaterialOffset + gltfMaterial.normalTexture.index;

		material.doubleSided = gltfMaterial.doubleSided;

		materials.push_back(material);
	}
	auto materialsBufferCI = vk::BufferCreateInfo{}
		.setSize(materials.size() * sizeof(Material))
		.setUsage(vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eStorageBuffer);
	materialsBuffer = std::make_unique<Buffer>(device, dmm, rth, materialsBufferCI, vk::ArrayProxyNoTemporaries{ static_cast<uint32_t>(materialsBufferCI.size), (char*)materials.data() }, MemoryStorage::DevicePersistent);

	LOG_INFO("Loading %d images", model.images.size());
	for (const auto& gltfImage : model.images) {
		texturePool.emplace_back(std::make_unique<Texture>(device, dmm, rth, path.parent_path() / std::filesystem::path(gltfImage.uri)));
	}

	// Load lights
	LOG_INFO("Loading %d lights", model.lights.size());
	uint32_t baseLightOffset = lightGlobalToTypeIndex.size();
	lightGlobalToTypeIndex.reserve(lightGlobalToTypeIndex.size() + model.lights.size());
	for (const auto& gltfLight : model.lights) {
		if (gltfLight.type == "point") {
			PointLight light;
			light.colour = glm::make_vec3(gltfLight.color.data());
			light.intensity = gltfLight.intensity;
			light.range = gltfLight.range;
			lightGlobalToTypeIndex.push_back(std::make_tuple(LightTypes::Point, static_cast<uint32_t>(pointLights.size())));
			pointLights.push_back(light);
		} else if (gltfLight.type == "directional") {
			DirectionalLight light;
			light.colour = glm::make_vec3(gltfLight.color.data());
			light.intensity = gltfLight.intensity;
			lightGlobalToTypeIndex.push_back(std::make_tuple(LightTypes::Directional, static_cast<uint32_t>(directionalLights.size())));
			directionalLights.push_back(light);
		}
	}

	LOG_INFO("Processing %d nodes", model.nodes.size());
	for (const auto& nodeIdx : model.scenes[0].nodes)
		processModelRecursive(parent, model, model.nodes[nodeIdx], glm::mat4(1.0f));

	// Light positions processed during scene traversal, so we upload these after it is done
	uint32_t numPointLights = pointLights.size();
	auto pointLightsBufferCI = vk::BufferCreateInfo{}
		.setSize(sizeof(uint32_t) + numPointLights * sizeof(PointLight))
		.setUsage(vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst);
	pointLightsBuffer = std::make_unique<Buffer>(device, dmm, rth, pointLightsBufferCI, nullptr, MemoryStorage::DevicePersistent);

	pointLightsBuffer->write({ sizeof(uint32_t), (char*)&numPointLights });
	if (numPointLights > 0)
		pointLightsBuffer->write({ static_cast<uint32_t>(numPointLights * sizeof(PointLight)), (char*)pointLights.data() }, sizeof(uint32_t));

	uint32_t numDirectionalLights = directionalLights.size();
	auto directionalLightsBufferCI = vk::BufferCreateInfo{}
		.setSize(sizeof(uint32_t) + numDirectionalLights * sizeof(DirectionalLight))
		.setUsage(vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst);
	directionalLightsBuffer = std::make_unique<Buffer>(device, dmm, rth, directionalLightsBufferCI, nullptr, MemoryStorage::DevicePersistent);

	directionalLightsBuffer->write({ sizeof(uint32_t), (char*)&numDirectionalLights });
	if (numDirectionalLights > 0)
		directionalLightsBuffer->write({ static_cast<uint32_t>(numDirectionalLights * sizeof(DirectionalLight)), (char*)directionalLights.data() }, sizeof(uint32_t));

	LOG_INFO("Finished loading model %s", path.filename().string().c_str());
}

void Scene::processModelRecursive(SceneObject* parent, const tinygltf::Model& model, const tinygltf::Node& node, const glm::mat4& parentTransform) {
	uint32_t baseMeshOffset = meshPool.size() - model.meshes.size();
	uint32_t baseMaterialOffset = materials.size() - model.materials.size();
	uint32_t baseLightOffset = lightGlobalToTypeIndex.size() - model.lights.size();

	int nodeMeshIdx = node.mesh != -1 ? baseMeshOffset + node.mesh : -1;

	glm::mat4 nodeTransform(1.0f);
	if (node.matrix.size() != 0) {
		nodeTransform = glm::make_mat4(node.matrix.data());
	} else {
		if (node.scale.size() != 0)
			nodeTransform = glm::scale(static_cast<glm::vec3>(glm::make_vec3(node.scale.data()))) * nodeTransform;
		if (node.rotation.size() != 0)
			nodeTransform = static_cast<glm::mat4>(glm::toMat4(glm::make_quat(node.rotation.data()))) * nodeTransform;
		if (node.translation.size() != 0)
			nodeTransform = glm::translate(static_cast<glm::vec3>(glm::make_vec3(node.translation.data()))) * nodeTransform;
	}
	glm::mat4 currentTransform = nodeTransform * parentTransform;

	if (node.light != -1) {
		glm::vec3 translation;
		glm::vec3 scale;
		glm::quat rotation;
		glm::vec3 dummy0;
		glm::vec4 dummy1;
		glm::decompose(currentTransform, dummy0, rotation, translation, dummy0, dummy1);

		auto&& [lightType, index] = lightGlobalToTypeIndex[baseLightOffset + node.light];
		if (lightType == LightTypes::Point) {
			pointLights[index].position = translation;
		} else if (lightType == LightTypes::Directional) {
			directionalLights[index].direction = rotation * glm::vec3(0.0, 0.0, -1.0);
		}
	}

	auto& so = addObject(parent ? parent : &root, nodeTransform, nodeMeshIdx);
	maxDepth = std::max(maxDepth, so.depth);
	for (const auto& childNodeIdx : node.children)
		processModelRecursive(&so, model, model.nodes[childNodeIdx], currentTransform);
}

}