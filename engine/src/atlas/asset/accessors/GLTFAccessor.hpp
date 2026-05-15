#pragma once

#include <glm/glm.hpp>
#include <tiny_gltf.h>

#include "IAssetAccessor.hpp"
#include "asset/AssetManager.hpp"

namespace Atlas {
    class GLTFAccessor : public ILoader {
    public:
        GLTFAccessor(AssetManager &assets, ExecutorService &service);
        ~GLTFAccessor() override = default;

        std::vector<std::string> extensions() const override { return {".gltf", ".glb"}; }

        std::vector<entt::entity> importAsset(const std::string &path, entt::registry &registry, entt::entity parentEntity) override;
        std::vector<std::byte> exportAsset(const std::vector<entt::entity> &entities, const entt::registry &registry) override;

    private:
        AssetManager &assets;
        ExecutorService &executor;

        void processNode(entt::registry &registry, const tinygltf::Model &model, int32_t nodeIdx, const glm::mat4 &parentTransform, const std::string &virtualPath, std::vector<entt::entity> &outEntities);
        void handleSkybox(entt::registry &registry, entt::entity entity, const tinygltf::Model &model);
        static void handlePostProcessing(entt::registry &registry, entt::entity entity, const tinygltf::Model &model);
        static AssetHandle resolveTexture(const tinygltf::Model &model, int texIdx, const std::vector<AssetHandle> &imageHandles);
        static glm::mat4 getNodeTransform(const tinygltf::Node &node);
    };
}
