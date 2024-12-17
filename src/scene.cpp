#include <scene.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/pbrmaterial.h>

namespace vkrt {

SceneObject::SceneObject(SceneObject* parent, glm::mat4& transform, std::vector<uint32_t> meshIndices)
	: transform(transform), parent(parent), meshIndices(meshIndices), depth(parent ? parent->depth + 1u : 0u) {}

Scene::Scene(vk::SharedDevice device, DeviceMemoryManager& dmm, ResourceCopyHandler& rch)
	: device(device), dmm(dmm), rch(rch), root(nullptr, glm::mat4(1.0f), {}), objectCount(0u), maxDepth(1u) {}

SceneObject& Scene::addObject(SceneObject* parent, glm::mat4& transform, std::vector<uint32_t> meshIndices) {
	objectCount++;
	auto& so = parent->children.emplace_back(parent ? parent : &root, transform, meshIndices);
	maxDepth = std::max(maxDepth, so.depth);
	return so;
}

void Scene::loadModel(SceneObject* parent, glm::mat4& transform, std::filesystem::path path) {
	Assimp::Importer importer;
	const aiScene* aiScene = importer.ReadFile(path.string(), aiProcess_GenNormals | aiProcess_CalcTangentSpace);
	if (!aiScene || aiScene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !aiScene->mRootNode) {
		LOG_ERROR("ERROR::ASSIMP::%s\n", importer.GetErrorString());
		return;
	}

	// Load meshes
	uint32_t baseMeshOffset = meshPool.size();
	uint32_t baseMaterialOffset = materials.size();
	std::vector<MeshInfo> meshInfos;
	meshInfos.reserve(aiScene->mNumMeshes);
	for (int i = 0; i < aiScene->mNumMeshes; i++) {
		auto& aiMesh = aiScene->mMeshes[i];
		std::vector<Vertex> vertices(aiMesh->mNumVertices);
		for (int v = 0; v < aiMesh->mNumVertices; v++) {
			memcpy(&vertices[v].position, &aiMesh->mVertices[v], sizeof(glm::vec3));
			memcpy(&vertices[v].normal, &aiMesh->mNormals[v], sizeof(glm::vec3));
			if (aiMesh->mTangents && aiMesh->mBitangents) {
				memcpy(&vertices[v].tangent, &aiMesh->mTangents[v], sizeof(glm::vec3));
				memcpy(&vertices[v].bitangent, &aiMesh->mBitangents[v], sizeof(glm::vec3));
			}
			if (aiScene->mMeshes[i]->mNumUVComponents[0] > 0u)
				memcpy(&vertices[v].uv0, &aiMesh->mTextureCoords[0][v], sizeof(glm::vec2));
			if (aiScene->mMeshes[i]->mNumUVComponents[1] > 0u)
				memcpy(&vertices[v].uv1, &aiMesh->mTextureCoords[1][v], sizeof(glm::vec2));
		}

		std::vector<uint32_t> indices;
		indices.reserve(aiMesh->mNumFaces * 3u);
		for (int f = 0u; f < aiMesh->mNumFaces; f++) {
			auto& face = aiMesh->mFaces[f];
			indices.push_back(face.mIndices[0]);
			indices.push_back(face.mIndices[1]);
			indices.push_back(face.mIndices[2]);
		}
		meshPool.emplace_back(device, dmm, rch, vertices, indices, baseMaterialOffset + aiMesh->mMaterialIndex);
		meshInfos.emplace_back(device->getBufferAddress(**meshPool.back().vertices), device->getBufferAddress(**meshPool.back().indices), meshPool.back().materialIdx);
	}
	auto geometryInfoBufferCI = vk::BufferCreateInfo{}
		.setSize(meshInfos.size() * sizeof(MeshInfo))
		.setUsage(vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eStorageBuffer);
	geometryInfoBuffer = std::make_unique<Buffer>(device, dmm, rch, geometryInfoBufferCI, vk::ArrayProxyNoTemporaries{ static_cast<uint32_t>(geometryInfoBufferCI.size), (char*)meshInfos.data() }, MemoryStorage::DevicePersistent);

	// Load materials and associated textures
	for (int i = 0; i < aiScene->mNumMaterials; i++) {
		auto& aiMaterial = aiScene->mMaterials[i];

		Material material;

		// Get property values
		aiMaterial->Get(AI_MATKEY_BASE_COLOR, material.baseColourFactor);
		aiMaterial->Get(AI_MATKEY_ROUGHNESS_FACTOR, material.roughnessFactor);
		aiMaterial->Get(AI_MATKEY_METALLIC_FACTOR, material.metallicFactor);
		aiMaterial->Get(AI_MATKEY_TWOSIDED, material.doubleSided);

		std::tuple baseColourProps = { aiTextureType_BASE_COLOR, &material.baseColourTexIdx };
		std::tuple roughnessMetallicProps = { aiTextureType_UNKNOWN, &material.roughnessMetallicTexIdx };
		std::tuple normalProps = { aiTextureType_NORMALS, &material.normalTexIdx };
		std::array matProps = { baseColourProps, roughnessMetallicProps, normalProps };

		for (auto& [textureType, textureIdxRef] : matProps) {
			aiString texFile;
			if (aiMaterial->GetTexture(textureType, 0, &texFile) == AI_SUCCESS) {
				auto [it, success] = texturesNameToIndex.try_emplace(texFile.C_Str(), texturesNameToIndex.size());
				if (success)
					texturePool.emplace_back(std::make_unique<Texture>(device, dmm, rch, path.parent_path() / std::filesystem::path(texFile.C_Str())));
				*textureIdxRef = std::get<uint32_t>(*it);
			}
		}
		materials.push_back(material);
	}
	auto materialsBufferCI = vk::BufferCreateInfo{}
		.setSize(materials.size() * sizeof(Material))
		.setUsage(vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eStorageBuffer);
	materialsBuffer = std::make_unique<Buffer>(device, dmm, rch, materialsBufferCI, vk::ArrayProxyNoTemporaries{ static_cast<uint32_t>(materialsBufferCI.size), (char*)materials.data() }, MemoryStorage::DevicePersistent);


	// Load lights
	for (int i = 0; i < aiScene->mNumLights; i++) {
		auto& aiLight = aiScene->mLights[i];
		if (aiLight->mType == aiLightSource_POINT) {
			PointLight light;
			memcpy(&light.position, &aiLight->mPosition, sizeof(glm::vec3));
			memcpy(&light.colour, &aiLight->mColorDiffuse, sizeof(glm::vec3));
			light.attenuationConstant = aiLight->mAttenuationConstant;
			light.attenuationLinear = aiLight->mAttenuationLinear;
			light.attenuationQuadratic = aiLight->mAttenuationQuadratic;

			lightNameToIndex[std::string(aiLight->mName.C_Str())] = std::make_tuple(LightTypes::Point, static_cast<uint32_t>(pointLights.size()));
			pointLights.push_back(light);
		} else if (aiLight->mType == aiLightSource_DIRECTIONAL) {
			DirectionalLight light;
			memcpy(&light.direction, &aiLight->mDirection, sizeof(glm::vec3));
			memcpy(&light.colour, &aiLight->mColorDiffuse, sizeof(glm::vec3));

			lightNameToIndex[std::string(aiLight->mName.C_Str())] = std::make_tuple(LightTypes::Directional, static_cast<uint32_t>(directionalLights.size()));
			directionalLights.push_back(light);
		}
	}

	processModelRecursive(parent, aiScene, aiScene->mRootNode, glm::mat4(1.0f), baseMeshOffset);
	importer.FreeScene();

	// Light positions processed during scene traversal, so we upload these after it is done
	auto pointLightsBufferCI = vk::BufferCreateInfo{}
		.setSize(sizeof(uint32_t) + pointLights.size() * sizeof(PointLight))
		.setUsage(vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eStorageBuffer);
	pointLightsBuffer = std::make_unique<Buffer>(device, dmm, rch, pointLightsBufferCI, nullptr, MemoryStorage::DevicePersistent);

	uint32_t numPointLights = pointLights.size();
	pointLightsBuffer->write({ sizeof(uint32_t), (char*)&numPointLights });
	pointLightsBuffer->write({ static_cast<uint32_t>(numPointLights * sizeof(PointLight)), (char*)pointLights.data() }, sizeof(uint32_t));

	auto directionalLightsBufferCI = vk::BufferCreateInfo{}
		.setSize(sizeof(uint32_t) + directionalLights.size() * sizeof(DirectionalLight))
		.setUsage(vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eStorageBuffer);
	directionalLightsBuffer = std::make_unique<Buffer>(device, dmm, rch, directionalLightsBufferCI, nullptr, MemoryStorage::DevicePersistent);

	uint32_t numDirectionalLights = directionalLights.size();
	directionalLightsBuffer->write({ sizeof(uint32_t), (char*)&numDirectionalLights });
	directionalLightsBuffer->write({ static_cast<uint32_t>(numDirectionalLights * sizeof(DirectionalLight)), (char*)directionalLights.data() }, sizeof(uint32_t));
}

void Scene::processModelRecursive(SceneObject* parent, const aiScene* aiScene, const aiNode* node, glm::mat4 parentTransform, uint32_t baseMeshOffset) {
	std::vector<uint32_t> nodeMeshIndices;
	for (int i = 0; i < node->mNumMeshes; i++)
		nodeMeshIndices.push_back(baseMeshOffset + node->mMeshes[i]);

	glm::mat4 nodeTransform;
	memcpy(&nodeTransform, &node->mTransformation, sizeof(nodeTransform));
	nodeTransform = glm::transpose(nodeTransform);

	glm::mat4 currentTransform = nodeTransform * parentTransform;
	glm::quat rotation;
	glm::vec3 translation, dummy0;
	glm::vec4 dummy1;
	glm::decompose(currentTransform, dummy0, rotation, translation, dummy0, dummy1);

	if (lightNameToIndex.find(std::string(node->mName.C_Str())) != lightNameToIndex.end()) {
		auto&& [lightType, index] = (*lightNameToIndex.find(std::string(node->mName.C_Str()))).second;
		if (lightType == LightTypes::Point) {
			pointLights[index].position = pointLights[index].position + translation;
		} else if (lightType == LightTypes::Directional) {
			directionalLights[index].direction = rotation * directionalLights[index].direction;
		}
	}

	auto& so = addObject(parent ? parent : &root, nodeTransform, nodeMeshIndices);
	maxDepth = std::max(maxDepth, so.depth);
	for (int i = 0; i < node->mNumChildren; i++) {
		processModelRecursive(&so, aiScene, node->mChildren[i], currentTransform, baseMeshOffset);
	}
}

}