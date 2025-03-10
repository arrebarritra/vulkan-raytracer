#include <scene.h>

#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace vkrt {

SceneObject::SceneObject(SceneObject* parent, glm::mat4& localTransform, int meshIdx)
	: localTransform(localTransform), worldTransform(parent ? parent->worldTransform * localTransform : localTransform), parent(parent), meshIdx(meshIdx), depth(parent ? parent->depth + 1u : 0u) {}

Scene::Scene(vk::SharedDevice device, DeviceMemoryManager& dmm, ResourceTransferHandler& rth)
	: device(device), dmm(dmm), rth(rth), root(nullptr, glm::mat4(1.0f), -1), objectCount(0u), maxDepth(1u) {}

SceneObject& Scene::addNode(SceneObject* parent, glm::mat4& localTransform, int meshIdx) {
	objectCount++;
	auto& so = parent->children.emplace_back(parent, localTransform, meshIdx);
	maxDepth = std::max(maxDepth, so.depth);
	return so;
}

void Scene::loadModel(std::filesystem::path path, SceneObject* parent, glm::mat4& localTransform) {
	LOG_INFO("Loading model \"%s\"", path.filename().string().c_str());
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
	uint32_t baseTextureOffset = texturePool.size();
	uint32_t baseLightOffset = lightGlobalToTypeIndex.size();
	meshPool.reserve(meshPool.size() + model.meshes.size());
	geometryInfos.reserve(geometryInfos.size() + model.meshes.size());
	materials.reserve(materials.size() + model.materials.size());
	bool validTangents = true;
	for (const auto& gltfMesh : model.meshes) {
		char progressBarText[200];
		snprintf(progressBarText, sizeof(progressBarText), "Loading \"%s\" (%d primitives)", gltfMesh.name.c_str(), gltfMesh.primitives.size());
		logProgressBar(meshPool.size() + 1 - baseMeshOffset, model.meshes.size(), 20, progressBarText);

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
				const float* texCoordsBuffer = nullptr;
				const float* tangentsBuffer = nullptr;
				size_t vertexCount = 0;

				{
					assert(gltfPrimitive.attributes.find("POSITION") != gltfPrimitive.attributes.end());
					const tinygltf::Accessor& accessor = model.accessors[gltfPrimitive.attributes.find("POSITION")->second];
					const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
					positionBuffer = reinterpret_cast<const float*>(&(model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
					vertexCount = accessor.count;
				}

				// Get buffer data for vertex normals
				{
					assert(gltfPrimitive.attributes.find("NORMAL") != gltfPrimitive.attributes.end());
					const tinygltf::Accessor& accessor = model.accessors[gltfPrimitive.attributes.find("NORMAL")->second];
					const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
					normalsBuffer = reinterpret_cast<const float*>(&(model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
				}

				// Get buffer data for vertex texture coordinates
				if (gltfPrimitive.attributes.find("TEXCOORD_0") != gltfPrimitive.attributes.end()) {
					const tinygltf::Accessor& accessor = model.accessors[gltfPrimitive.attributes.find("TEXCOORD_0")->second];
					const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
					texCoordsBuffer = reinterpret_cast<const float*>(&(model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
				}
				if (model.materials[gltfPrimitive.material].normalTexture.index != -1) assert(texCoordsBuffer);

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
					vertex.normal = glm::normalize(glm::vec3(glm::make_vec3(&normalsBuffer[v * 3])));
					vertex.uv = texCoordsBuffer ? glm::make_vec2(&texCoordsBuffer[v * 2]) : glm::vec2(0.0f);
					vertex.tangent = tangentsBuffer ? glm::make_vec4(&tangentsBuffer[v * 4]) : glm::vec4(0.0f);
					if (tangentsBuffer && glm::cross(vertex.normal, glm::vec3(vertex.tangent)) == glm::vec3(0.0)) validTangents = false;
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
		meshPool.emplace_back(device, dmm, rth, geometryInfos.size(), primitiveVertices, primitiveIndices, materialIndices);
		for (int i = 0; i < gltfMesh.primitives.size(); i++)
			geometryInfos.emplace_back(device->getBufferAddress(**meshPool.back().vertexBuffers[i]), device->getBufferAddress(**meshPool.back().indexBuffers[i]), meshPool.back().materialIndices[i]);
	}
	logProgressBarFinish(model.meshes.size(), 20, "");
	if (!validTangents) LOG_ERROR("Mesh contains invalid tangents");

	// Load materials and associated textures
	if (model.materials.size() > 0) {
		LOG_INFO("Loading %d materials", model.materials.size());
		for (const auto& gltfMaterial : model.materials) {
			char progressBarText[200];
			snprintf(progressBarText, sizeof(progressBarText), "Loading \"%s\"", gltfMaterial.name.c_str());
			logProgressBar(materials.size() + 1 - baseMaterialOffset, model.materials.size(), 20, progressBarText);
			Material material;

			// PBR metallic-roughness
			material.baseColourFactor = glm::make_vec4(gltfMaterial.pbrMetallicRoughness.baseColorFactor.data());
			if (gltfMaterial.pbrMetallicRoughness.baseColorTexture.index != -1)
				material.baseColourTexIdx = baseTextureOffset + model.textures[gltfMaterial.pbrMetallicRoughness.baseColorTexture.index].source;

			material.metallicFactor = gltfMaterial.pbrMetallicRoughness.metallicFactor;
			material.roughnessFactor = gltfMaterial.pbrMetallicRoughness.roughnessFactor;
			if (gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index != -1)
				material.metallicRoughnessTexIdx = baseTextureOffset + model.textures[gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index].source;
			if (gltfMaterial.normalTexture.index != -1)
				material.normalTexIdx = baseTextureOffset + model.textures[gltfMaterial.normalTexture.index].source;

			// Alpha
			if (gltfMaterial.alphaMode == "OPAQUE")
				material.alphaMode = 0;
			else if (gltfMaterial.alphaMode == "MASK")
				material.alphaMode = 1;
			else if (gltfMaterial.alphaMode == "BLEND")
				material.alphaMode = 2;
			material.alphaCutoff = gltfMaterial.alphaCutoff;

			material.emissiveFactor = glm::make_vec3(gltfMaterial.emissiveFactor.data());
			if (gltfMaterial.emissiveTexture.index != -1)
				material.emissiveTexIdx = baseTextureOffset + model.textures[gltfMaterial.emissiveTexture.index].source;

			// Emissive strength
			if (auto emissiveStrength = gltfMaterial.extensions.find("KHR_materials_emissive_strength"); emissiveStrength != gltfMaterial.extensions.end()) {
				if (emissiveStrength->second.Has("emissiveStrength"))
					material.emissiveFactor *= emissiveStrength->second.Get("emissiveStrength").GetNumberAsDouble();
			}

			// Transmission 
			if (auto transmission = gltfMaterial.extensions.find("KHR_materials_transmission"); transmission != gltfMaterial.extensions.end()) {
				if (transmission->second.Has("transmissionFactor"))
					material.transmissionFactor = static_cast<float>(transmission->second.Get("transmissionFactor").GetNumberAsDouble());
				if (transmission->second.Has("transmissionTexture"))
					material.transmissionTexIdx = baseTextureOffset + model.textures[transmission->second.Get("transmissionTexture").Get("index").GetNumberAsInt()].source;
			}

			// Volume
			if (auto volume = gltfMaterial.extensions.find("KHR_materials_volume"); volume != gltfMaterial.extensions.end()) {
				if (volume->second.Has("thicknessFactor"))
					material.thicknessFactor = volume->second.Get("thicknessFactor").GetNumberAsDouble();

				float attenuationDistance = std::numeric_limits<float>::infinity();
				glm::vec3 attenuationColour = glm::vec3(1.0f);
				if (volume->second.Has("attenuationDistance"))
					attenuationDistance = volume->second.Get("attenuationDistance").GetNumberAsDouble();
				if (volume->second.Has("attenuationColor")) {
					assert(volume->second.Get("attenuationColor").ArrayLen() == 3);
					for (int i = 0; i < 3; i++)
						attenuationColour[i] = volume->second.Get("attenuationColor").Get(i).GetNumberAsDouble();
				}
				material.attenuationCoefficient = -glm::log(attenuationColour) / attenuationDistance;
			}

			// Anisotropy
			if (auto anisotropy = gltfMaterial.extensions.find("KHR_materials_anisotropy"); anisotropy != gltfMaterial.extensions.end()) {
				if (anisotropy->second.Has("anisotropyStrength"))
					material.anisotropyStrength = static_cast<float>(anisotropy->second.Get("anisotropyStrength").GetNumberAsDouble());
				if (anisotropy->second.Has("anisotropyRotation"))
					material.anisotropyStrength = static_cast<float>(anisotropy->second.Get("anisotropyRotation").GetNumberAsDouble());
				if (anisotropy->second.Has("anisotropyTexture"))
					material.anisotropyTexIdx = baseTextureOffset + model.textures[anisotropy->second.Get("anisotropyTexture").Get("index").GetNumberAsInt()].source;
			}

			// Dispersion
			if (auto dispersion = gltfMaterial.extensions.find("KHR_materials_dispersion"); dispersion != gltfMaterial.extensions.end()) {
				if (dispersion->second.Has("dispersion"))
					material.dispersion = static_cast<float>(dispersion->second.Get("dispersion").GetNumberAsDouble());
			}

			materials.push_back(material);
		}
		logProgressBarFinish(model.materials.size(), 20, "");
	}

	if (model.images.size() > 0) {
		LOG_INFO("Loading %d images", model.images.size());
		for (const auto& gltfImage : model.images) {
			char progressBarText[200];
			snprintf(progressBarText, sizeof(progressBarText), "Loading \"%s\"", gltfImage.uri.c_str());
			logProgressBar(texturePool.size() + 1 - baseTextureOffset, model.images.size(), 20, progressBarText);
			texturePool.emplace_back(std::make_unique<Texture>(device, dmm, rth, path.parent_path() / std::filesystem::path(gltfImage.uri)));
			rth.freeCompletedTransfers();
		}
		logProgressBarFinish(model.images.size(), 20, "");
	}

	// Load lights
	if (model.lights.size() > 0) {
		LOG_INFO("Loading %d punctual lights", model.lights.size());
		lightGlobalToTypeIndex.reserve(lightGlobalToTypeIndex.size() + model.lights.size());
		for (const auto& gltfLight : model.lights) {
			char progressBarText[200];
			snprintf(progressBarText, sizeof(progressBarText), "Loading \"%s\"", gltfLight.name);
			logProgressBar(lightGlobalToTypeIndex.size() + 1 - baseLightOffset, model.lights.size(), 20, progressBarText);

			if (gltfLight.type == "point") {
				PointLight light;
				light.colour = gltfLight.color.size() == 0 ? glm::vec3(0.0f) : glm::make_vec3(gltfLight.color.data());
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
		logProgressBarFinish(lightGlobalToTypeIndex.size() + 1 - baseLightOffset, 20, "");
	}

	LOG_INFO("Processing scene graph with <=%d nodes", model.nodes.size());
	auto& modelRoot = addNode(parent, localTransform);
	uint32_t baseObjectCount = this->objectCount;
	for (const auto& nodeIdx : model.scenes[0].nodes)
		processModelRecursive(&modelRoot, model, model.nodes[nodeIdx], baseObjectCount);
	logProgressBarFinish(this->objectCount - baseObjectCount, 20, "");
	LOG_INFO("Finished loading model %s", path.filename().string().c_str());
}

void Scene::uploadResources() {
	LOG_INFO("Uploading scene resources to GPU");
	auto geometryInfoBufferCI = vk::BufferCreateInfo{}
		.setSize(geometryInfos.size() * sizeof(GeometryInfo))
		.setUsage(vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eStorageBuffer);
	geometryInfoBuffer = std::make_unique<Buffer>(device, dmm, rth, geometryInfoBufferCI, vk::ArrayProxyNoTemporaries{ static_cast<uint32_t>(geometryInfoBufferCI.size), (char*)geometryInfos.data() }, MemoryStorage::DevicePersistent);

	if (emissiveTriangles.size() > 0) {
		LOG_INFO("Normalising probability heuristic for %d emissive triangles (%d primitives)", emissiveTriangles.size(), emissiveSurfaces.size());
		float totalHeuristic = emissiveTriangles.back().pHeuristic;
		for (auto& emissiveTriangle : emissiveTriangles) emissiveTriangle.pHeuristic /= totalHeuristic;
	}

	auto materialsBufferCI = vk::BufferCreateInfo{}
		.setSize(materials.size() * sizeof(Material))
		.setUsage(vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eStorageBuffer);
	materialsBuffer = std::make_unique<Buffer>(device, dmm, rth, materialsBufferCI, vk::ArrayProxyNoTemporaries{ static_cast<uint32_t>(materialsBufferCI.size), (char*)materials.data() }, MemoryStorage::DevicePersistent);

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

	// Emissive surfaces and heuristic are also processed during scene traversal
	uint32_t numEmissiveSurfaces = emissiveSurfaces.size();
	auto emissiveSurfaceBufferCI = vk::BufferCreateInfo{}
		.setSize(sizeof(uint32_t) + numEmissiveSurfaces * sizeof(EmissiveSurface))
		.setUsage(vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst);
	emissiveSurfacesBuffer = std::make_unique<Buffer>(device, dmm, rth, emissiveSurfaceBufferCI, nullptr, MemoryStorage::DevicePersistent);

	emissiveSurfacesBuffer->write({ sizeof(uint32_t), (char*)&numEmissiveSurfaces });
	if (numEmissiveSurfaces > 0)
		emissiveSurfacesBuffer->write({ static_cast<uint32_t>(numEmissiveSurfaces * sizeof(EmissiveSurface)), (char*)emissiveSurfaces.data() }, sizeof(uint32_t));

	uint32_t numEmissiveTriangles = emissiveTriangles.size();
	auto emissiveTriangleBufferCI = vk::BufferCreateInfo{}
		.setSize(sizeof(uint32_t) + numEmissiveTriangles * sizeof(EmissiveTriangle))
		.setUsage(vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst);
	emissiveTrianglesBuffer = std::make_unique<Buffer>(device, dmm, rth, emissiveTriangleBufferCI, nullptr, MemoryStorage::DevicePersistent);

	emissiveTrianglesBuffer->write({ sizeof(uint32_t), (char*)&numEmissiveTriangles });
	if (numEmissiveTriangles > 0)
		emissiveTrianglesBuffer->write({ static_cast<uint32_t>(numEmissiveTriangles * sizeof(EmissiveTriangle)), (char*)emissiveTriangles.data() }, sizeof(uint32_t));

	LOG_INFO("Scene resources uploaded");
}

void Scene::processModelRecursive(SceneObject* parent, const tinygltf::Model& model, const tinygltf::Node& node, uint32_t baseObjectCount) {
	char progressBarText[200];
	snprintf(progressBarText, sizeof(progressBarText), "(~) Processing \"%s\"", node.name.c_str());
	logProgressBar(this->objectCount + 1 - baseObjectCount, model.nodes.size(), 20, progressBarText);

	uint32_t baseMeshOffset = meshPool.size() - model.meshes.size();
	uint32_t baseMaterialOffset = materials.size() - model.materials.size();
	uint32_t baseLightOffset = lightGlobalToTypeIndex.size() - model.lights.size();

	int nodeMeshIdx = node.mesh != -1 ? baseMeshOffset + node.mesh : -1;

	glm::mat4 localTransform(1.0f);
	if (node.matrix.size() != 0) {
		localTransform = glm::make_mat4(node.matrix.data());
	} else {
		if (node.scale.size() != 0)
			localTransform = glm::scale(static_cast<glm::vec3>(glm::make_vec3(node.scale.data()))) * localTransform;
		if (node.rotation.size() != 0)
			localTransform = glm::mat4(static_cast<glm::quat>(glm::make_quat(node.rotation.data()))) * localTransform;
		if (node.translation.size() != 0)
			localTransform = glm::translate(static_cast<glm::vec3>(glm::make_vec3(node.translation.data()))) * localTransform;
	}

	glm::mat4 worldTransform = parent->worldTransform * localTransform;
	if (node.light != -1) {
		glm::vec3 translation;
		glm::vec3 scale;
		glm::quat rotation;
		glm::vec3 dummy0;
		glm::vec4 dummy1;
		glm::decompose(worldTransform, dummy0, rotation, translation, dummy0, dummy1);

		auto&& [lightType, index] = lightGlobalToTypeIndex[baseLightOffset + node.light];
		if (lightType == LightTypes::Point) {
			pointLights[index].position = translation;
		} else if (lightType == LightTypes::Directional) {
			directionalLights[index].direction = rotation * glm::vec3(0.0, 0.0, -1.0);
		}
	}

	if (nodeMeshIdx != -1) {
		const Mesh& mesh = meshPool[nodeMeshIdx];
		for (int i = 0; i < mesh.primitiveCount; i++) {
			if (mesh.materialIndices[i] >= 0 && materials[mesh.materialIndices[i]].emissiveFactor != glm::vec3(0.0)) {
				const auto& gltfPrimitive = model.meshes[node.mesh].primitives[i];
				EmissiveSurface es;
				es.geometryIdx = meshPool[nodeMeshIdx].primitiveOffset + i;
				es.baseEmissiveTriangleIdx = emissiveTriangles.size();
				es.transform = worldTransform;
				geometryInfos[mesh.primitiveOffset + i].emissiveSurfaceIdx = emissiveSurfaces.size();
				emissiveSurfaces.push_back(es);
				processEmissivePrimitive(model, gltfPrimitive, worldTransform);
			}
		}
	}

	auto& so = addNode(parent, localTransform, nodeMeshIdx);
	maxDepth = std::max(maxDepth, so.depth);
	for (const auto& childNodeIdx : node.children)
		processModelRecursive(&so, model, model.nodes[childNodeIdx], baseObjectCount);
}

// TODO: move to compute shader and account for emissive texture
void Scene::processEmissivePrimitive(const tinygltf::Model& model, const tinygltf::Primitive& primitive, const glm::mat4 worldTransform) {
	const float* positionBuffer = nullptr;

	std::vector<Index> indices;
	{
		const tinygltf::Accessor& accessor = model.accessors[primitive.indices];
		const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
		const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];

		// glTF supports different component types of indices
		switch (accessor.componentType) {
			case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT: {
				const uint32_t* buf = reinterpret_cast<const uint32_t*>(&buffer.data[accessor.byteOffset + bufferView.byteOffset]);
				indices.resize(accessor.count);
				memcpy(indices.data(), buf, sizeof(uint32_t) * accessor.count);
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
	}
	{
		assert(primitive.attributes.find("POSITION") != primitive.attributes.end());
		const tinygltf::Accessor& accessor = model.accessors[primitive.attributes.find("POSITION")->second];
		const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
		positionBuffer = reinterpret_cast<const float*>(&(model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
		size_t vertexCount = accessor.count;
	}

	// Calculate intensity/area heuristic for each triangle
	uint32_t baseMaterialOffset = materials.size() - model.materials.size();
	Material& mat = materials[primitive.material - baseMaterialOffset];
	for (size_t i = 0; i < indices.size() / 3; i++) {
		std::array v = {
			glm::vec3(worldTransform * glm::vec4(glm::make_vec3(&positionBuffer[3 * indices[3 * i]]), 1.0f)),
			glm::vec3(worldTransform * glm::vec4(glm::make_vec3(&positionBuffer[3 * indices[3 * i + 1]]), 1.0f)),
			glm::vec3(worldTransform * glm::vec4(glm::make_vec3(&positionBuffer[3 * indices[3 * i + 2]]), 1.0f))
		};
		float area = glm::length(glm::cross(v[1] - v[0], v[2] - v[0])) / 2.0f;
		float heuristic = area * glm::dot(mat.emissiveFactor, glm::vec3(0.2126, 0.7152, 0.0722));
		emissiveTriangles.push_back({ (emissiveTriangles.size() > 0 ? emissiveTriangles.back().pHeuristic : 0.0f) + heuristic });
	}
}

}