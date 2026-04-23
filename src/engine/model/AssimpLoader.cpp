#include "AssimpLoader.h"
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <sstream>
#include <stdexcept>

void AssimpLoader::Initialize(TextureManager *textureManager,
                              MeshManager *meshManager,
                              MaterialManager *materialManager) {
    meshLoader_.Initialize(textureManager, meshManager, materialManager);
}

Model AssimpLoader::Load(const std::string &path) {
    if (!meshLoader_.IsInitialized()) {
        throw std::runtime_error("AssimpLoader is not initialized");
    }

    Assimp::Importer importer;

    const aiScene *scene = importer.ReadFile(
        path, aiProcess_Triangulate | aiProcess_FlipUVs |
                  aiProcess_JoinIdenticalVertices | aiProcess_LimitBoneWeights);

    if (!scene || !scene->HasMeshes()) {
        std::ostringstream oss;
        oss << "[AssimpLoader] Load failed. path='" << path
            << "' error='" << importer.GetErrorString() << "'";
        throw std::runtime_error(oss.str());
    }

    Model model{};
    meshLoader_.LoadMeshes(scene, path, model);
    animationLoader_.LoadAnimations(scene, model);

    model.finalBoneMatrices.resize(model.bones.size());
    return model;
}
