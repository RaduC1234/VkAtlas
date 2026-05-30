#pragma once

#include "Panel.hpp"

#include <Atlas.hpp>
#include <imgui.h>

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace Atlas::Editor {
    class AssetExplorerPanel final : public Panel {
    public:
        explicit AssetExplorerPanel(ProjectLayer &projectLayer);
        ~AssetExplorerPanel() override;

        void onDetach() override;
        void onImGuiRender() override;

    private:
        enum class AssetKind {
            Folder,
            Level,
            Material,
            Model,
            Texture,
            Script,
            Other
        };

        struct AssetEntry {
            std::filesystem::path path;
            std::filesystem::path relativePath;
            std::string name;
            AssetKind kind = AssetKind::Other;
            bool directory = false;
        };

        ProjectLayer &projectLayer;
        std::filesystem::path cachedAssetRoot;
        std::filesystem::path currentDirectory;
        std::filesystem::path selectedPath;
        std::filesystem::path previewTexturePath;
        std::string searchText;
        float tileSize = 96.0f;
        float previewZoom = 1.0f;
        bool texturePreviewOpen = false;

        struct TexturePreview {
            AssetHandle<Texture> handle;
            VkDescriptorSet descriptor = VK_NULL_HANDLE;
            bool loadFailed = false;
        };

        std::unordered_map<std::string, TexturePreview> texturePreviews;

        void syncProjectRoot();
        void clearPreviewCache();
        void renderToolbar(const std::filesystem::path &assetRoot);
        void renderFolderTree(const std::filesystem::path &assetRoot);
        void renderFolderNode(const std::filesystem::path &directory, const std::filesystem::path &assetRoot);
        void renderTileGrid(const std::filesystem::path &assetRoot);
        void renderTexturePreviewWindow(const std::filesystem::path &assetRoot);
        void drawTileIcon(AssetKind kind, const ImVec2 &min, const ImVec2 &max, bool selected) const;
        bool drawTexturePreview(const AssetEntry &entry, const ImVec2 &min, const ImVec2 &max, const std::filesystem::path &assetRoot);
        bool ensureTexturePreview(const std::filesystem::path &path, const std::filesystem::path &assetRoot, VkDescriptorSet &descriptor, Texture *&texture);
        std::vector<AssetEntry> collectEntries(const std::filesystem::path &directory, const std::filesystem::path &assetRoot) const;

        static AssetKind classify(const std::filesystem::path &path, bool directory);
        static bool isTextureKind(AssetKind kind);
        static const char *kindLabel(AssetKind kind);
        static ImU32 kindColor(AssetKind kind);
        static std::string lower(std::string value);
        static std::string displayPath(const std::filesystem::path &path);
        static std::filesystem::path relativeTo(const std::filesystem::path &path, const std::filesystem::path &root);
    };
}
