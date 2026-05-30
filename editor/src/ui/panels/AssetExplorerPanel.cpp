#include "AssetExplorerPanel.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <system_error>

#include <imgui.h>

#include "core/Log.hpp"
#include "renderer/ImGuiLayer.hpp"

namespace Atlas::Editor {
    AssetExplorerPanel::AssetExplorerPanel(ProjectLayer &projectLayer) : projectLayer(projectLayer) {
    }

    AssetExplorerPanel::~AssetExplorerPanel() {
        clearPreviewCache();
    }

    void AssetExplorerPanel::onDetach() {
        clearPreviewCache();
    }

    void AssetExplorerPanel::onImGuiRender() {
        if (!visible) {
            return;
        }

        syncProjectRoot();

        ImGui::SetNextWindowSize(ImVec2(760.0f, 420.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Asset Explorer", &visible)) {
            ImGui::End();
            return;
        }

        const std::filesystem::path &assetRoot = cachedAssetRoot;
        if (assetRoot.empty() || !std::filesystem::exists(assetRoot)) {
            ImGui::TextUnformatted("No project assets folder");
            ImGui::End();
            return;
        }
        if (currentDirectory.empty() || !std::filesystem::exists(currentDirectory)) {
            currentDirectory = assetRoot;
        }

        renderToolbar(assetRoot);
        ImGui::Separator();

        const float treeWidth = 230.0f;
        ImGui::BeginChild("##asset_tree", ImVec2(treeWidth, 0.0f), true);
        renderFolderTree(assetRoot);
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("##asset_tiles", ImVec2(0.0f, 0.0f), false);
        renderTileGrid(assetRoot);
        ImGui::EndChild();

        ImGui::End();
        renderTexturePreviewWindow(assetRoot);
    }

    void AssetExplorerPanel::syncProjectRoot() {
        const std::filesystem::path assetRoot = projectLayer.project().assetsPath();
        if (assetRoot == cachedAssetRoot) {
            return;
        }

        cachedAssetRoot = assetRoot;
        currentDirectory = cachedAssetRoot;
        selectedPath.clear();
        previewTexturePath.clear();
        searchText.clear();
        texturePreviewOpen = false;
        previewZoom = 1.0f;
        clearPreviewCache();
    }

    void AssetExplorerPanel::clearPreviewCache() {
        for (auto &[_, preview]: texturePreviews) {
            if (preview.descriptor != VK_NULL_HANDLE) {
                ImGuiLayer::removeTexture(preview.descriptor);
                preview.descriptor = VK_NULL_HANDLE;
            }
        }

        texturePreviews.clear();
    }

    void AssetExplorerPanel::renderToolbar(const std::filesystem::path &assetRoot) {
        const bool atRoot = currentDirectory.empty() || currentDirectory == assetRoot;
        if (!atRoot && ImGui::Button("<")) {
            currentDirectory = currentDirectory.parent_path();
            if (currentDirectory.empty() || relativeTo(currentDirectory, assetRoot).string().starts_with("..")) {
                currentDirectory = assetRoot;
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Assets")) {
            currentDirectory = assetRoot;
        }

        ImGui::SameLine();
        const std::filesystem::path relativeCurrent = relativeTo(currentDirectory, assetRoot);
        const std::string breadcrumb = relativeCurrent.empty() || relativeCurrent == "."
            ? std::string("Assets")
            : "Assets/" + displayPath(relativeCurrent);
        ImGui::TextUnformatted(breadcrumb.c_str());

        ImGui::Spacing();
        ImGui::TextUnformatted("Size");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        ImGui::SliderFloat("##asset_tile_size", &tileSize, 72.0f, 160.0f, "%.0f");

        ImGui::SameLine(0.0f, 10.0f);
        ImGui::SetNextItemWidth(-1.0f);
        char searchBuffer[256]{};
        std::strncpy(searchBuffer, searchText.c_str(), sizeof(searchBuffer) - 1);
        if (ImGui::InputTextWithHint("##asset_search", "Search assets", searchBuffer, sizeof(searchBuffer))) {
            searchText = searchBuffer;
        }
    }

    void AssetExplorerPanel::renderFolderTree(const std::filesystem::path &assetRoot) {
        const ImGuiTreeNodeFlags rootFlags = ImGuiTreeNodeFlags_OpenOnArrow |
                                             ImGuiTreeNodeFlags_OpenOnDoubleClick |
                                             ImGuiTreeNodeFlags_DefaultOpen |
                                             (currentDirectory == assetRoot ? ImGuiTreeNodeFlags_Selected : 0);

        const bool open = ImGui::TreeNodeEx("Assets", rootFlags);
        if (ImGui::IsItemClicked()) {
            currentDirectory = assetRoot;
        }

        if (open) {
            renderFolderNode(assetRoot, assetRoot);
            ImGui::TreePop();
        }
    }

    void AssetExplorerPanel::renderFolderNode(const std::filesystem::path &directory, const std::filesystem::path &assetRoot) {
        std::vector<std::filesystem::path> children;
        std::error_code error;
        for (const auto &entry: std::filesystem::directory_iterator(directory, error)) {
            if (entry.is_directory(error)) {
                children.push_back(entry.path());
            }
        }

        std::ranges::sort(children, [](const auto &lhs, const auto &rhs) {
            return lower(lhs.filename().string()) < lower(rhs.filename().string());
        });

        for (const auto &child: children) {
            const bool selected = currentDirectory == child;
            const bool hasChildren = !collectEntries(child, assetRoot).empty();
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
            if (selected) {
                flags |= ImGuiTreeNodeFlags_Selected;
            }
            if (!hasChildren) {
                flags |= ImGuiTreeNodeFlags_Leaf;
            }

            const bool open = ImGui::TreeNodeEx(child.filename().string().c_str(), flags);
            if (ImGui::IsItemClicked()) {
                currentDirectory = child;
            }

            if (open) {
                renderFolderNode(child, assetRoot);
                ImGui::TreePop();
            }
        }
    }

    void AssetExplorerPanel::renderTileGrid(const std::filesystem::path &assetRoot) {
        const std::vector<AssetEntry> entries = collectEntries(currentDirectory, assetRoot);
        const std::string query = lower(searchText);

        const float availableWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        const float spacing = 14.0f;
        const int columns = std::max(1, static_cast<int>((availableWidth + spacing) / (tileSize + spacing)));
        const float resolvedTileSize = std::floor((availableWidth - spacing * static_cast<float>(columns - 1)) / static_cast<float>(columns));
        const float labelHeight = 40.0f;
        const float rowHeight = resolvedTileSize + labelHeight + spacing;
        const ImVec2 gridStart = ImGui::GetCursorScreenPos();

        int rendered = 0;
        for (const AssetEntry &entry: entries) {
            const std::string searchable = lower(entry.name + " " + displayPath(entry.relativePath));
            if (!query.empty() && searchable.find(query) == std::string::npos) {
                continue;
            }

            const int row = rendered / columns;
            const int column = rendered % columns;
            const ImVec2 tileMin(
                gridStart.x + static_cast<float>(column) * (resolvedTileSize + spacing),
                gridStart.y + static_cast<float>(row) * rowHeight
            );
            ImGui::SetCursorScreenPos(tileMin);

            ImGui::PushID(entry.path.string().c_str());

            const bool selected = selectedPath == entry.path;
            const ImVec2 tileMax(tileMin.x + resolvedTileSize, tileMin.y + resolvedTileSize);
            ImGui::InvisibleButton("##tile", ImVec2(resolvedTileSize, resolvedTileSize + labelHeight));
            const bool hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked()) {
                selectedPath = entry.path;
            }
            if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && entry.directory) {
                currentDirectory = entry.path;
                selectedPath.clear();
            }
            if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && isTextureKind(entry.kind)) {
                previewTexturePath = entry.path;
                texturePreviewOpen = true;
                previewZoom = 1.0f;
            }

            ImDrawList *drawList = ImGui::GetWindowDrawList();
            const ImU32 frameColor = selected
                ? IM_COL32(72, 126, 190, 255)
                : hovered ? IM_COL32(72, 72, 78, 255) : IM_COL32(43, 43, 48, 255);
            drawList->AddRectFilled(tileMin, tileMax, frameColor, 6.0f);
            drawList->AddRect(tileMin, tileMax, IM_COL32(24, 24, 26, 255), 6.0f);

            const ImVec2 previewMin(tileMin.x + resolvedTileSize * 0.10f, tileMin.y + resolvedTileSize * 0.10f);
            const ImVec2 previewMax(tileMin.x + resolvedTileSize * 0.90f, tileMin.y + resolvedTileSize * 0.78f);
            if (!drawTexturePreview(entry, previewMin, previewMax, assetRoot)) {
                const ImVec2 iconMin(tileMin.x + resolvedTileSize * 0.22f, tileMin.y + resolvedTileSize * 0.20f);
                const ImVec2 iconMax(tileMin.x + resolvedTileSize * 0.78f, tileMin.y + resolvedTileSize * 0.74f);
                drawTileIcon(entry.kind, iconMin, iconMax, selected);
            }

            const ImVec2 textPos(tileMin.x, tileMax.y + 6.0f);
            ImGui::SetCursorScreenPos(textPos);
            ImGui::PushTextWrapPos(tileMin.x + resolvedTileSize);
            ImGui::TextUnformatted(entry.name.c_str());
            ImGui::PopTextWrapPos();

            if (hovered) {
                ImGui::SetTooltip("%s\n%s", kindLabel(entry.kind), displayPath(entry.relativePath).c_str());
            }

            ImGui::PopID();
            ++rendered;
        }

        if (rendered == 0) {
            ImGui::TextUnformatted("No assets");
        } else {
            const int rows = (rendered + columns - 1) / columns;
            ImGui::SetCursorScreenPos(gridStart);
            ImGui::Dummy(ImVec2(availableWidth, static_cast<float>(rows) * rowHeight));
        }
    }

    void AssetExplorerPanel::renderTexturePreviewWindow(const std::filesystem::path &assetRoot) {
        if (!texturePreviewOpen) {
            return;
        }

        if (previewTexturePath.empty() || !std::filesystem::exists(previewTexturePath)) {
            texturePreviewOpen = false;
            previewTexturePath.clear();
            return;
        }

        std::string title = previewTexturePath.filename().string() + "###Texture Preview";
        ImGui::SetNextWindowSize(ImVec2(720.0f, 520.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(title.c_str(), &texturePreviewOpen)) {
            ImGui::End();
            return;
        }

        VkDescriptorSet descriptor = VK_NULL_HANDLE;
        Texture *texture = nullptr;
        if (!ensureTexturePreview(previewTexturePath, assetRoot, descriptor, texture) || descriptor == VK_NULL_HANDLE || !texture) {
            ImGui::TextUnformatted("Loading preview...");
            ImGui::End();
            return;
        }

        ImGui::TextUnformatted(displayPath(relativeTo(previewTexturePath, assetRoot)).c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%ux%u", texture->width(), texture->height());

        ImGui::SetNextItemWidth(160.0f);
        ImGui::SliderFloat("Zoom", &previewZoom, 0.1f, 4.0f, "%.1fx");

        ImGui::Separator();

        const ImVec2 available = ImGui::GetContentRegionAvail();
        const float textureWidth = static_cast<float>(std::max(1u, texture->width()));
        const float textureHeight = static_cast<float>(std::max(1u, texture->height()));
        const float scale = std::min(available.x / textureWidth, available.y / textureHeight) * previewZoom;
        const ImVec2 imageSize(
            std::max(1.0f, std::floor(textureWidth * scale)),
            std::max(1.0f, std::floor(textureHeight * scale))
        );

        const ImVec2 cursor = ImGui::GetCursorPos();
        const float centeredX = cursor.x + std::max(0.0f, (available.x - imageSize.x) * 0.5f);
        ImGui::SetCursorPosX(centeredX);
        ImGui::Image((ImTextureID) descriptor, imageSize);

        ImGui::End();
    }

    void AssetExplorerPanel::drawTileIcon(AssetKind kind, const ImVec2 &min, const ImVec2 &max, bool selected) const {
        ImDrawList *drawList = ImGui::GetWindowDrawList();
        const ImU32 color = kindColor(kind);
        const ImU32 shadow = selected ? IM_COL32(18, 28, 40, 180) : IM_COL32(18, 18, 20, 180);

        const float width = max.x - min.x;
        const float height = max.y - min.y;
        const ImVec2 shadowOffset(2.0f, 3.0f);

        if (kind == AssetKind::Folder) {
            const ImVec2 tabMax(min.x + width * 0.45f, min.y + height * 0.24f);
            drawList->AddRectFilled(ImVec2(min.x + shadowOffset.x, min.y + height * 0.08f + shadowOffset.y),
                                    ImVec2(max.x + shadowOffset.x, max.y + shadowOffset.y), shadow, 5.0f);
            drawList->AddRectFilled(ImVec2(min.x, min.y + height * 0.08f), tabMax, color, 4.0f);
            drawList->AddRectFilled(ImVec2(min.x, min.y + height * 0.20f), max, color, 5.0f);
            return;
        }

        drawList->AddRectFilled(ImVec2(min.x + shadowOffset.x, min.y + shadowOffset.y),
                                ImVec2(max.x + shadowOffset.x, max.y + shadowOffset.y), shadow, 5.0f);
        drawList->AddRectFilled(min, max, color, 5.0f);

        const ImVec2 foldA(max.x - width * 0.28f, min.y);
        const ImVec2 foldB(max.x, min.y + height * 0.28f);
        drawList->AddTriangleFilled(foldA, ImVec2(max.x, min.y), foldB, IM_COL32(255, 255, 255, 70));

        const char *label = kindLabel(kind);
        const ImVec2 textSize = ImGui::CalcTextSize(label);
        const ImVec2 textPos(min.x + (width - textSize.x) * 0.5f, min.y + (height - textSize.y) * 0.58f);
        drawList->AddText(textPos, IM_COL32(245, 245, 245, 235), label);
    }

    bool AssetExplorerPanel::drawTexturePreview(
        const AssetEntry &entry,
        const ImVec2 &min,
        const ImVec2 &max,
        const std::filesystem::path &assetRoot) {
        if (!isTextureKind(entry.kind)) {
            return false;
        }

        VkDescriptorSet descriptor = VK_NULL_HANDLE;
        Texture *texture = nullptr;
        if (!ensureTexturePreview(entry.path, assetRoot, descriptor, texture) || descriptor == VK_NULL_HANDLE) {
            return false;
        }

        ImVec2 imageMin = min;
        ImVec2 imageMax = max;
        if (texture && texture->width() > 0 && texture->height() > 0) {
            const float availableWidth = max.x - min.x;
            const float availableHeight = max.y - min.y;
            const float aspect = static_cast<float>(texture->width()) / static_cast<float>(texture->height());
            float width = availableWidth;
            float height = width / aspect;
            if (height > availableHeight) {
                height = availableHeight;
                width = height * aspect;
            }

            imageMin.x = min.x + (availableWidth - width) * 0.5f;
            imageMin.y = min.y + (availableHeight - height) * 0.5f;
            imageMax.x = imageMin.x + width;
            imageMax.y = imageMin.y + height;
        }

        ImDrawList *drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(min, max, IM_COL32(24, 24, 26, 255), 4.0f);
        drawList->AddImage((ImTextureID) descriptor, imageMin, imageMax);
        drawList->AddRect(min, max, IM_COL32(18, 18, 20, 180), 4.0f);
        return true;
    }

    bool AssetExplorerPanel::ensureTexturePreview(
        const std::filesystem::path &path,
        const std::filesystem::path &assetRoot,
        VkDescriptorSet &descriptor,
        Texture *&texture) {
        const std::string virtualPath = displayPath(relativeTo(path, assetRoot));
        TexturePreview &preview = texturePreviews[virtualPath];
        if (!preview.handle.valid() && !preview.loadFailed) {
            try {
                preview.handle = projectLayer.assetManager().store<Texture>(virtualPath);
            } catch (const std::exception &error) {
                preview.loadFailed = true;
                AT_WARN("AssetExplorerPanel: failed to load texture preview '{}': {}", virtualPath, error.what());
            }
        }

        if (!preview.handle.valid() || !preview.handle.isReady()) {
            return false;
        }

        if (preview.descriptor == VK_NULL_HANDLE) {
            const VkDescriptorImageInfo info = preview.handle.descriptor();
            preview.descriptor = ImGuiLayer::addTexture(info.sampler, info.imageView, info.imageLayout);
        }

        descriptor = preview.descriptor;
        texture = preview.handle.get();
        return descriptor != VK_NULL_HANDLE && texture != nullptr;
    }

    std::vector<AssetExplorerPanel::AssetEntry> AssetExplorerPanel::collectEntries(
        const std::filesystem::path &directory,
        const std::filesystem::path &assetRoot) const {
        std::vector<AssetEntry> entries;
        std::error_code error;
        if (directory.empty() || !std::filesystem::exists(directory, error)) {
            return entries;
        }

        for (const auto &entry: std::filesystem::directory_iterator(directory, error)) {
            const bool directoryEntry = entry.is_directory(error);
            AssetEntry assetEntry{};
            assetEntry.path = entry.path();
            assetEntry.relativePath = relativeTo(assetEntry.path, assetRoot);
            assetEntry.name = assetEntry.path.filename().string();
            assetEntry.directory = directoryEntry;
            assetEntry.kind = classify(assetEntry.path, directoryEntry);
            entries.push_back(std::move(assetEntry));
        }

        std::ranges::sort(entries, [](const AssetEntry &lhs, const AssetEntry &rhs) {
            if (lhs.directory != rhs.directory) {
                return lhs.directory > rhs.directory;
            }

            return lower(lhs.name) < lower(rhs.name);
        });

        return entries;
    }

    AssetExplorerPanel::AssetKind AssetExplorerPanel::classify(const std::filesystem::path &path, bool directory) {
        if (directory) {
            return AssetKind::Folder;
        }

        std::string extension = lower(path.extension().string());
        if (extension == ".atlaslevel") {
            return AssetKind::Level;
        }
        if (extension == ".atlasmaterial" || extension == ".mat") {
            return AssetKind::Material;
        }
        if (extension == ".gltf" || extension == ".glb" || extension == ".obj" || extension == ".fbx") {
            return AssetKind::Model;
        }
        if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp" ||
            extension == ".tga" || extension == ".hdr" || extension == ".ktx" || extension == ".ktx2") {
            return AssetKind::Texture;
        }
        if (extension == ".cpp" || extension == ".hpp" || extension == ".h" || extension == ".cs" || extension == ".lua") {
            return AssetKind::Script;
        }

        return AssetKind::Other;
    }

    bool AssetExplorerPanel::isTextureKind(AssetKind kind) {
        return kind == AssetKind::Texture;
    }

    const char *AssetExplorerPanel::kindLabel(AssetKind kind) {
        switch (kind) {
            case AssetKind::Folder: return "DIR";
            case AssetKind::Level: return "LVL";
            case AssetKind::Material: return "MAT";
            case AssetKind::Model: return "MDL";
            case AssetKind::Texture: return "TEX";
            case AssetKind::Script: return "SRC";
            case AssetKind::Other: return "FILE";
        }

        return "FILE";
    }

    ImU32 AssetExplorerPanel::kindColor(AssetKind kind) {
        switch (kind) {
            case AssetKind::Folder: return IM_COL32(217, 166, 67, 255);
            case AssetKind::Level: return IM_COL32(88, 154, 217, 255);
            case AssetKind::Material: return IM_COL32(178, 111, 202, 255);
            case AssetKind::Model: return IM_COL32(93, 181, 138, 255);
            case AssetKind::Texture: return IM_COL32(205, 116, 98, 255);
            case AssetKind::Script: return IM_COL32(105, 128, 196, 255);
            case AssetKind::Other: return IM_COL32(130, 136, 145, 255);
        }

        return IM_COL32(130, 136, 145, 255);
    }

    std::string AssetExplorerPanel::lower(std::string value) {
        std::ranges::transform(value, value.begin(), [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        return value;
    }

    std::string AssetExplorerPanel::displayPath(const std::filesystem::path &path) {
        return path.generic_string();
    }

    std::filesystem::path AssetExplorerPanel::relativeTo(const std::filesystem::path &path, const std::filesystem::path &root) {
        std::error_code error;
        std::filesystem::path relative = std::filesystem::relative(path, root, error);
        if (error) {
            return path.filename();
        }

        return relative;
    }
}
