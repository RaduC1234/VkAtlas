#pragma once

#include "asset/AssetManager.hpp"
#include "project/importers/IAssetImporter.hpp"

namespace Atlas::Editor {
    class OBJImporter final : public IAssetImporter {
    public:
        explicit OBJImporter(AssetManager &assets);
        ~OBJImporter() override = default;

        std::vector<std::string> extensions() const override { return {".obj"}; }

        void importAsset(const std::string &path, EntityBuffer &buffer) override;

    private:
        AssetManager &assets;
    };
}
