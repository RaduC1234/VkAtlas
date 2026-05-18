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

        std::vector<entt::entity> importAsset(const std::string &path, entt::registry &registry, entt::entity parentEntity) override;
        std::vector<std::byte> exportAsset(const std::vector<entt::entity> &entities, const entt::registry &registry) override;

    private:
        AssetManager &assets;
        ExecutorService &executor;

        void processNode(entt::registry &registry, const tinygltf::Model &model, int32_t nodeIdx, const glm::mat4 &parentTransform, entt::entity parentEntity, const std::vector<std::vector<AssetHandle<Mesh>>> &meshHandles, const std::vector<AssetHandle<Texture>> &imageHandles, std::vector<entt::entity> &outEntities);
        void handleSkybox(entt::registry &registry, entt::entity entity, const tinygltf::Model &model);
        static void handlePostProcessing(entt::registry &registry, entt::entity entity, const tinygltf::Model &model);
        static AssetHandle<Texture> resolveTexture(const tinygltf::Model &model, int texIdx, const std::vector<AssetHandle<Texture>> &imageHandles);
        static glm::mat4 getNodeTransform(const tinygltf::Node &node);
    };
}
