#include <scene.h>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>

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
	const aiScene* aiScene = importer.ReadFile(path.string(), aiProcess_Triangulate);
	if (!aiScene || aiScene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !aiScene->mRootNode) {
		LOG_ERROR("ERROR::ASSIMP::%s\n", importer.GetErrorString());
		return;
	}

	uint32_t baseMeshOffset = meshPool.size();
	for (uint32_t i = 0; i < aiScene->mNumMeshes; i++) {
		std::vector<glm::vec3> vertices(aiScene->mMeshes[i]->mNumVertices);
		memcpy(vertices.data(), aiScene->mMeshes[i]->mVertices, sizeof(glm::vec3) * aiScene->mMeshes[i]->mNumVertices);
		std::vector<uint32_t> indices;
		indices.reserve(aiScene->mMeshes[i]->mNumFaces * 3u);
		for (uint32_t f = 0u; f < aiScene->mMeshes[i]->mNumFaces; f++) {
			auto& face = aiScene->mMeshes[i]->mFaces[f];
			indices.push_back(face.mIndices[0]);
			indices.push_back(face.mIndices[1]);
			indices.push_back(face.mIndices[2]);
		}
		meshPool.emplace_back(device, dmm, rch, vertices, indices);
	}
	processModelRecursive(parent, aiScene, aiScene->mRootNode, baseMeshOffset);
}

void Scene::processModelRecursive(SceneObject* parent, const aiScene* aiScene, const aiNode* node, uint32_t baseMeshOffset) {
	std::vector<uint32_t> nodeMeshIndices;
	for (int i = 0; i < node->mNumMeshes; i++)
		nodeMeshIndices.push_back(baseMeshOffset + node->mMeshes[i]);

	glm::mat4 nodeTransform;
	memcpy(&nodeTransform, &node->mTransformation, sizeof(nodeTransform));
	auto& so = addObject(parent ? parent : &root, nodeTransform, nodeMeshIndices);
	maxDepth = std::max(maxDepth, so.depth);
	for (int i = 0; i < node->mNumChildren; i++) {
		processModelRecursive(&so, aiScene, node->mChildren[i], baseMeshOffset);
	}
}

}