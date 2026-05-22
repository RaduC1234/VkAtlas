#pragma once

#include "IAssetAccessor.hpp"
#include "asset/AssetManager.hpp"

namespace Atlas {
    class OBJAccessor : public IAccessor {
    public:
        OBJAccessor(AssetManager &assets, ExecutorService &service);
        ~OBJAccessor() override = default;

        std::vector<std::string> extensions() const override { return {".obj"}; }

        void importAsset(const std::string &path, EntityBuffer &buffer) override;
        std::vector<std::byte> exportAsset(const std::vector<entt::entity> &entities, const entt::registry &registry) override;

    private:
        AssetManager &assets;
        ExecutorService &executor;
    };
}
