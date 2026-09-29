#pragma once

#include <glm/glm.hpp>
#include <tiny_gltf.h>

#include "asset/AssetManager.hpp"
#include "project/importers/IAssetImporter.hpp"

namespace Atlas::Editor {
    class GLTFImporter final : public IAssetImporter {
    public:
        explicit GLTFImporter(AssetManager &assets);
        ~GLTFImporter() override = default;

        std::vector<std::string> extensions() const override { return {".gltf", ".glb"}; }

        void importAsset(const std::string &path, EntityBuffer &buffer) override;

    private:
        AssetManager &assets;

        std::vector<AssetHandle<Texture>> decodeAndStoreTextures(const std::vector<tinygltf::Image> &images, const std::string &path) const;
        void processNode(EntityBuffer &buffer, const tinygltf::Model &model, int32_t nodeIdx, const glm::mat4 &parentTransform, const std::vector<std::vector<AssetHandle<Mesh>>> &meshHandles, const std::vector<AssetHandle<Texture>> &imageHandles, const std::string &sourcePath);
        void handleSkybox(EntityBuffer &buffer, const tinygltf::Model &model, bool &skyboxAdded);
        static void handlePostProcessing(EntityBuffer &buffer, const tinygltf::Model &model, bool &postProcessingAdded);
        static AssetHandle<Texture> resolveTexture(const tinygltf::Model &model, int texIdx, const std::vector<AssetHandle<Texture>> &imageHandles);
        static glm::mat4 getNodeTransform(const tinygltf::Node &node);
    };
}
