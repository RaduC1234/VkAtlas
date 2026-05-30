#pragma once

#include <Atlas.hpp>

#include <string>

#include "core/EditorHistory.hpp"

namespace Atlas::Editor {
    struct MaterialEditState {
        bool active = false;
        AssetHandle<Material> handle;
        Material before;
    };

    class MaterialEditor {
    public:
        static bool drawProperties(
            ProjectLayer &projectLayer,
            EditorHistory &history,
            entt::registry *registry,
            entt::entity ownerEntity,
            AssetHandle<Material> materialHandle,
            MaterialEditState &state
        );

        static std::string displayName(const std::string &path, AssetHandle<Material> materialHandle);

    private:
        static bool drawTextureSlot(ProjectLayer &projectLayer, const char *label, AssetHandle<Texture> &texture);
        static void patchMaterialUsers(entt::registry *registry, AssetHandle<Material> materialHandle);
        static bool isInternalTexturePath(const std::string &path);
        static std::string textureDisplayName(const std::string &path);
        static std::string buildTextureFilter();
    };
}
