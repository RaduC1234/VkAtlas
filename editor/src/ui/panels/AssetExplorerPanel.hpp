#pragma once

#include "Panel.hpp"
#include "core/IconRegistry.hpp"

#include <Atlas.hpp>
#include <imgui.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Atlas::Editor {
    class AssetExplorerPanel final : public Panel {
    public:
        AssetExplorerPanel(ProjectLayer &projectLayer, IconRegistry &iconRegistry);
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
            Cubemap,
            Audio,
            Shader,
            Script,
            Font,
            Other
        };

        enum class ViewMode { Grid, List, Detail };
        enum class SortKey { Name, Type, Size, Date };

        enum class FilterKind {
            All, Model, Material, Texture, Shader, Font, Other
        };

        struct AssetEntry {
            std::filesystem::path path;
            std::filesystem::path relativePath;
            std::string name;
            // OPT: pre-lowercased name cached at snapshot build time,
            //      avoids per-frame allocations in applyFiltersAndSort
            std::string nameLower;
            AssetKind kind = AssetKind::Other;
            bool directory = false;
            uintmax_t bytes = 0;
            std::string formattedSize;
            std::string formattedDate;
            std::filesystem::file_time_type lastWriteTime;
        };

        struct FolderNode {
            std::filesystem::path path;
            std::string name;
            std::vector<FolderNode> children;
        };

        // OPT: entries and folderTree are now refreshed on independent schedules.
        // entries  — rebuilt whenever currentDirectory changes (fast: one flat scan).
        // folderTree — rebuilt only when assetRoot changes or a folder is added/deleted
        //              (slow: full recursive scan), on a much longer interval.
        struct AssetSnapshot {
            std::filesystem::path assetRoot;
            std::filesystem::path directory;
            std::vector<AssetEntry> entries;
            FolderNode folderTree;
            uint64_t generation = 0;
            uint64_t treeGeneration = 0;  // tracks which tree version is embedded
            bool valid = false;
        };

        ProjectLayer &projectLayer;
        IconRegistry &iconRegistry;
        std::filesystem::path cachedAssetRoot;
        std::filesystem::path currentDirectory;
        std::filesystem::path selectedPath;
        std::filesystem::path contextMenuPath;
        std::filesystem::path previewTexturePath;
        std::string searchText;
        bool texturePreviewOpen = false;
        float previewZoom = 1.0f;

        ViewMode viewMode = ViewMode::Grid;
        SortKey sortKey = SortKey::Name;
        int sortDir = 1;
        FilterKind filterKind = FilterKind::All;

        bool sortMenuOpen = false;
        bool filterMenuOpen = false;
        bool contextMenuOpen = false;

        struct TexturePreview {
            AssetHandle<Texture> handle;
            VkDescriptorSet descriptor = VK_NULL_HANDLE;
            bool loadFailed = false;
        };

        std::unordered_map<std::string, TexturePreview> texturePreviews;
        AssetSnapshot assetSnapshot;

        // OPT: separate futures for entries vs. folder tree so each can be
        //      scheduled independently at different rates.
        std::future<AssetSnapshot> assetRefreshFuture;
        std::future<FolderNode>   treeRefreshFuture;

        std::chrono::steady_clock::time_point lastAssetRefresh{};
        std::chrono::steady_clock::time_point lastTreeRefresh{};
        uint64_t requestedRefreshGeneration = 0;
        uint64_t appliedRefreshGeneration = 0;
        uint64_t requestedTreeGeneration = 0;
        uint64_t appliedTreeGeneration = 0;
        bool assetRefreshRequested = true;
        bool treeRefreshRequested  = true;

        // OPT: cached filtered+sorted result — only recomputed when inputs change,
        //      not every frame.
        std::vector<AssetEntry> cachedFilteredEntries;
        std::string    cachedFilterSearch;
        SortKey        cachedFilterSortKey   = SortKey::Name;
        int            cachedFilterSortDir   = 1;
        FilterKind     cachedFilterKind      = FilterKind::All;
        uint64_t       cachedFilterGeneration = 0;

        // Entries refresh every 15 s; tree refresh every 60 s.
        // The tree is expensive (full recursive scan); entries are cheap (one flat scan).
        static constexpr std::chrono::seconds AssetRefreshInterval{15};
        static constexpr std::chrono::seconds TreeRefreshInterval{60};

        void syncProjectRoot();
        void clearPreviewCache();
        void requestAssetRefresh();
        void requestTreeRefresh();
        void pollAssetRefresh();
        void pollTreeRefresh();
        void scheduleAssetRefreshIfNeeded();
        void scheduleTreeRefreshIfNeeded();

        void renderToolbar(const std::filesystem::path &assetRoot);
        void renderFolderTree(const std::filesystem::path &assetRoot);
        void renderFolderNode(const FolderNode &node);
        void renderTileGrid(const std::vector<AssetEntry> &entries, const std::filesystem::path &assetRoot);
        void renderListView(const std::vector<AssetEntry> &entries, const std::filesystem::path &assetRoot);
        void renderDetailView(const std::vector<AssetEntry> &entries, const std::filesystem::path &assetRoot);
        void renderDetailStrip(const std::vector<AssetEntry> &allEntries);
        void renderContextMenu(const std::filesystem::path &assetRoot);
        void renderTexturePreviewWindow(const std::filesystem::path &assetRoot);

        void drawTile(const AssetEntry &entry, const ImVec2 &min, float size, bool selected, bool hovered,
                      const std::filesystem::path &assetRoot);
        bool drawTexturePreview(const AssetEntry &entry, const ImVec2 &min, const ImVec2 &max,
                                const std::filesystem::path &assetRoot);
        bool ensureTexturePreview(const std::filesystem::path &path, const std::filesystem::path &assetRoot,
                                  VkDescriptorSet &descriptor, Texture *&texture);

        static AssetSnapshot buildAssetSnapshot(std::filesystem::path assetRoot, std::filesystem::path directory,
                                                uint64_t generation, uint64_t treeGeneration,
                                                FolderNode existingTree);
        static FolderNode buildFolderTree(const std::filesystem::path &directory);
        static std::vector<AssetEntry> collectEntries(const std::filesystem::path &directory,
                                                      const std::filesystem::path &assetRoot);

        // OPT: returns a reference into cachedFilteredEntries; rebuilds only when dirty.
        const std::vector<AssetEntry> &filteredEntries(const std::vector<AssetEntry> &raw);

        const AssetEntry *findSelected(const std::vector<AssetEntry> &entries) const;

        static AssetKind classify(const std::filesystem::path &path, bool directory);
        static bool isTextureKind(AssetKind kind);
        static bool matchesFilter(AssetKind kind, FilterKind filter);
        static const char *kindBadge(AssetKind kind);
        static const char *kindName(AssetKind kind);
        static ImU32 kindColor(AssetKind kind);
        static ImU32 kindColorBg(AssetKind kind);
        static std::string extensionBadge(const std::filesystem::path &path);
        static std::string formatSize(uintmax_t bytes);
        static std::string formatDate(const std::filesystem::file_time_type &time);
        static std::string lower(std::string value);
        static std::string displayPath(const std::filesystem::path &path);
        static std::filesystem::path relativeTo(const std::filesystem::path &path,
                                                const std::filesystem::path &root);
    };
}