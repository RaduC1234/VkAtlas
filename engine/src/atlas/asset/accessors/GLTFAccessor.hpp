#pragma once

#include <glm/glm.hpp>
#include <tiny_gltf.h>

#include "IAssetAccessor.hpp"
#include "asset/AssetManager.hpp"

namespace Atlas {
    class GLTFAccessor : public IAccessor {
    public:
        GLTFAccessor(AssetManager &assets, ExecutorService &service);
        ~GLTFAccessor() override = default;

        std::vector<std::string> extensions() const override { return {".gltf", ".glb"}; }

        void importAsset(const std::string &path, EntityBuffer &buffer) override;
        std::vector<std::byte> exportAsset(const std::vector<entt::entity> &entities, const entt::registry &registry) override;

    private:
        AssetManager &assets;
        ExecutorService &executor;

        std::vector<AssetHandle<Texture>> decodeAndStoreTextures(const std::vector<tinygltf::Image> &images, const std::string &path);
        void processNode(EntityBuffer &buffer, const tinygltf::Model &model, int32_t nodeIdx, const glm::mat4 &parentTransform, const std::vector<std::vector<AssetHandle<Mesh>>> &meshHandles, const std::vector<AssetHandle<Texture>> &imageHandles, const std::string &sourcePath);
        void handleSkybox(EntityBuffer &buffer, const tinygltf::Model &model, bool &skyboxAdded);
        static void handlePostProcessing(EntityBuffer &buffer, const tinygltf::Model &model, bool &postProcessingAdded);
        static AssetHandle<Texture> resolveTexture(const tinygltf::Model &model, int texIdx, const std::vector<AssetHandle<Texture>> &imageHandles);
        static glm::mat4 getNodeTransform(const tinygltf::Node &node);
    };
}
