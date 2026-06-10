#include "AssetExplorerPanel.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <system_error>

#include <imgui.h>

#include "core/IconRegistry.hpp"
#include "core/ImGuiLayer.hpp"
#include "core/Log.hpp"
#include "core/Profiler.hpp"
#include "ui/components/AssetExplorerComponents.hpp"
#include "ui/theme/EditorTheme.hpp"

namespace Atlas::Editor {
    using AEC = AssetExplorerComponents;

    // ── lifecycle ────────────────────────────────────────────────────────────

    AssetExplorerPanel::AssetExplorerPanel(ProjectLayer &projectLayer, IconRegistry &iconRegistry)
        : projectLayer(projectLayer), iconRegistry(iconRegistry) {
    }

    AssetExplorerPanel::~AssetExplorerPanel() {
        clearPreviewCache();
    }

    void AssetExplorerPanel::onDetach() {
        clearPreviewCache();
    }

    // ── main render ──────────────────────────────────────────────────────────

    void AssetExplorerPanel::onImGuiRender() {
        if (!visible) return;

        {
            ATLAS_PROFILE_SCOPE("AssetExplorerPanel::syncProjectRoot");
            syncProjectRoot();
        }
        {
            ATLAS_PROFILE_SCOPE("AssetExplorerPanel::pollAssetRefresh");
            pollAssetRefresh();
        }
        // OPT: poll the tree future independently from the entries future
        {
            ATLAS_PROFILE_SCOPE("AssetExplorerPanel::pollTreeRefresh");
            pollTreeRefresh();
        }

        ImGui::SetNextWindowSize(ImVec2(860.0f, 480.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Asset Explorer", &visible)) {
            ImGui::End();
            return;
        }

        const std::filesystem::path &assetRoot = cachedAssetRoot;
        if (assetRoot.empty()) {
            ImGui::TextUnformatted("No project assets folder");
            ImGui::End();
            return;
        }
        if (currentDirectory.empty()) {
            currentDirectory = assetRoot;
            requestAssetRefresh();
        }

        {
            ATLAS_PROFILE_SCOPE("AssetExplorerPanel::scheduleAssetRefreshIfNeeded");
            scheduleAssetRefreshIfNeeded();
        }
        // OPT: schedule tree refresh on its own slower cadence
        {
            ATLAS_PROFILE_SCOPE("AssetExplorerPanel::scheduleTreeRefreshIfNeeded");
            scheduleTreeRefreshIfNeeded();
        }

        const bool snapshotMatches =
                assetSnapshot.valid &&
                assetSnapshot.assetRoot == assetRoot &&
                assetSnapshot.directory == currentDirectory;
        const std::vector<AssetEntry> emptyEntries;
        const std::vector<AssetEntry> &rawEntries = snapshotMatches
                                                       ? assetSnapshot.entries
                                                       : emptyEntries;

        // OPT: filteredEntries() is cached — only recomputed when search/sort/filter
        //      state or the snapshot generation actually changes, not every frame.
        const std::vector<AssetEntry> &entries = filteredEntries(rawEntries);

        // Toolbar
        {
            ATLAS_PROFILE_SCOPE("AssetExplorerPanel::renderToolbar");
            renderToolbar(assetRoot);
        }

        // Body: tree | content area
        const float bodyH = -AEC::stripHeight();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        ImGui::BeginChild("##ae_body", ImVec2(0, bodyH), false, ImGuiWindowFlags_NoScrollbar);
        ImGui::PopStyleVar(2);

        // Left tree
        ImGui::PushStyleColor(ImGuiCol_ChildBg, AEC::colorVec4(AEC::Color::TreeBg));
        ImGui::BeginChild("##ae_tree", ImVec2(AEC::treeWidth(), 0), false);
        ImGui::PopStyleColor();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
        ImGui::SetCursorPos(ImVec2(6, 6));
        {
            ATLAS_PROFILE_SCOPE("AssetExplorerPanel::renderFolderTree");
            renderFolderTree(assetRoot);
        }
        ImGui::PopStyleVar();
        ImGui::EndChild();

        ImGui::SameLine(0, 0);

        // Vertical separator
        ImDrawList *bg = ImGui::GetWindowDrawList();
        ImVec2 sepMin = ImGui::GetCursorScreenPos();
        AEC::separatorLine(bg, sepMin, ImVec2(sepMin.x, sepMin.y + ImGui::GetContentRegionAvail().y));
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 1.0f);

        // Right content
        ImGui::BeginChild("##ae_content", ImVec2(0, 0), false);
        const float pad = viewMode == ViewMode::Grid ? 14.0f : 10.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
        ImGui::SetCursorPos(ImVec2(pad, pad));

        if (entries.empty() && !searchText.empty()) {
            ImGui::Spacing();
            ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - 160.0f) * 0.5f);
            ImGui::TextDisabled("No assets match \"%s\"", searchText.c_str());
        } else if (entries.empty()) {
            ImGui::Spacing();
            ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - 80.0f) * 0.5f);
            ImGui::TextDisabled("No assets");
        } else if (viewMode == ViewMode::Grid) {
            ATLAS_PROFILE_SCOPE("AssetExplorerPanel::renderTileGrid");
            renderTileGrid(entries, assetRoot);
        } else if (viewMode == ViewMode::List) {
            ATLAS_PROFILE_SCOPE("AssetExplorerPanel::renderListView");
            renderListView(entries, assetRoot);
        } else {
            ATLAS_PROFILE_SCOPE("AssetExplorerPanel::renderDetailView");
            renderDetailView(entries, assetRoot);
        }

        ImGui::PopStyleVar();

        // Close context menu if clicked outside
        if (contextMenuOpen && ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            contextMenuOpen = false;

        ImGui::EndChild();
        ImGui::EndChild(); // body

        // Detail strip
        {
            ATLAS_PROFILE_SCOPE("AssetExplorerPanel::renderDetailStrip");
            renderDetailStrip(entries);
        }

        // Context menu popup
        {
            ATLAS_PROFILE_SCOPE("AssetExplorerPanel::renderContextMenu");
            renderContextMenu(assetRoot);
        }

        ImGui::End();
        {
            ATLAS_PROFILE_SCOPE("AssetExplorerPanel::renderTexturePreviewWindow");
            renderTexturePreviewWindow(assetRoot);
        }
    }

    // ── toolbar ──────────────────────────────────────────────────────────────

    void AssetExplorerPanel::renderToolbar(const std::filesystem::path &assetRoot) {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        ImVec2 toolbarMin = ImGui::GetCursorScreenPos();

        // Toolbar background
        const float toolbarH = AEC::toolbarHeight();
        ImVec2 winSize = ImGui::GetContentRegionAvail();
        dl->AddRectFilled(toolbarMin, ImVec2(toolbarMin.x + winSize.x, toolbarMin.y + toolbarH), AEC::color(AEC::Color::Toolbar));

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 3));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
        ImGui::SetCursorScreenPos(ImVec2(toolbarMin.x + 8, toolbarMin.y + 6));

        // Back button
        const bool atRoot = (currentDirectory == assetRoot);
        if (atRoot) ImGui::BeginDisabled();
        if (ImGui::ArrowButton("##ae_back", ImGuiDir_Left)) {
            currentDirectory = currentDirectory.parent_path();
            if (currentDirectory.empty() || relativeTo(currentDirectory, assetRoot).string().starts_with(".."))
                currentDirectory = assetRoot;
            selectedPath.clear();
            requestAssetRefresh();
        }
        if (atRoot) ImGui::EndDisabled();

        ImGui::SameLine(0, 6);

        // Breadcrumbs
        {
            std::filesystem::path rel = relativeTo(currentDirectory, assetRoot);
            std::vector<std::string> crumbs;
            crumbs.push_back("Assets");
            if (!rel.empty() && rel != ".") {
                for (const auto &part: rel) {
                    if (!part.empty() && part != ".")
                        crumbs.push_back(part.string());
                }
            }

            std::vector<std::filesystem::path> crumbPaths;
            crumbPaths.push_back(assetRoot);
            if (crumbs.size() > 1) {
                std::filesystem::path p = assetRoot;
                for (size_t i = 1; i < crumbs.size(); ++i) {
                    p = p / crumbs[i];
                    crumbPaths.push_back(p);
                }
            }

            for (size_t i = 0; i < crumbs.size(); ++i) {
                if (i > 0) {
                    ImGui::SameLine(0, 2);
                    ImGui::TextDisabled("›");
                    ImGui::SameLine(0, 2);
                }

                const bool isCurrent = (i == crumbs.size() - 1);
                if (AEC::breadcrumbButton(crumbs[i].c_str(), isCurrent)) {
                    currentDirectory = crumbPaths[i];
                    selectedPath.clear();
                    requestAssetRefresh();
                }
            }
        }

        // Right-side controls: compute total width then jump to it
        const float searchW = 180.0f;
        const float btnW = 26.0f;
        const float segW = 3 * 26.0f + 6.0f;
        const float rightW = searchW + 4 + btnW + 4 + btnW + 9 + segW + 4 + btnW + 12;
        float rightX = toolbarMin.x + winSize.x - rightW;
        ImGui::SameLine(rightX - toolbarMin.x - ImGui::GetWindowPos().x + ImGui::GetScrollX());

        // Search box
        ImGui::SetNextItemWidth(searchW);
        char buf[256]{};
        std::strncpy(buf, searchText.c_str(), sizeof(buf) - 1);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, EditorTheme::color(EditorTheme::Color::Surface));
        if (ImGui::InputTextWithHint("##ae_search", "Search assets", buf, sizeof(buf)))
            searchText = buf;
        ImGui::PopStyleColor();

        ImGui::SameLine(0, 4);

        // Sort button
        const bool sortActive = (sortKey != SortKey::Name || sortDir != 1);
        {
            const auto &ic = iconRegistry.get("sort", 16);
            if (ic.valid() ? AEC::toolbarIconButton("##ae_sort", ic.textureId(), ic.size(), "Sort", sortActive, ImVec2(btnW, 0))
                           : AEC::toolbarButton("S##ae_sort", "Sort", sortActive, ImVec2(btnW, 0)))
                sortMenuOpen = !sortMenuOpen;
        }

        // Sort popup
        if (sortMenuOpen) {
            const ImVec2 itemMin = ImGui::GetItemRectMin();
            const ImVec2 itemSize = ImGui::GetItemRectSize();
            ImGui::SetNextWindowPos(ImVec2(itemMin.x, itemMin.y + itemSize.y + 2));
            ImGui::SetNextWindowSize(ImVec2(160, 0));
            if (ImGui::BeginPopup("##ae_sortpop")) {
                ImGui::TextDisabled("SORT BY");
                ImGui::Separator();
                const char *sortLabels[] = {"Name", "Type", "Size", "Date modified"};
                for (int i = 0; i < 4; i++) {
                    const SortKey sk = static_cast<SortKey>(i);
                    bool active = (sortKey == sk);
                    if (active) ImGui::PushStyleColor(ImGuiCol_Text, EditorTheme::color(EditorTheme::Color::AccentHover));
                    if (ImGui::MenuItem(sortLabels[i])) {
                        if (sortKey == sk) sortDir = -sortDir;
                        else {
                            sortKey = sk;
                            sortDir = 1;
                        }
                        sortMenuOpen = false;
                    }
                    if (active) ImGui::PopStyleColor();
                    if (active) {
                        ImGui::SameLine();
                        ImGui::TextDisabled(sortDir > 0 ? "↑" : "↓");
                    }
                }
                ImGui::EndPopup();
            }
            ImGui::OpenPopup("##ae_sortpop");
        }

        ImGui::SameLine(0, 4);

        // Filter button
        const bool filterActive = (filterKind != FilterKind::All);
        {
            const auto &ic = iconRegistry.get("filter", 16);
            if (ic.valid() ? AEC::toolbarIconButton("##ae_filter", ic.textureId(), ic.size(), "Filter by type", filterActive, ImVec2(btnW, 0))
                           : AEC::toolbarButton("F##ae_filter", "Filter by type", filterActive, ImVec2(btnW, 0)))
                filterMenuOpen = !filterMenuOpen;
        }

        if (filterMenuOpen) {
            const ImVec2 itemMin = ImGui::GetItemRectMin();
            const ImVec2 itemSize = ImGui::GetItemRectSize();
            ImGui::SetNextWindowPos(ImVec2(itemMin.x, itemMin.y + itemSize.y + 2));
            ImGui::SetNextWindowSize(ImVec2(150, 0));
            if (ImGui::BeginPopup("##ae_filterpop")) {
                ImGui::TextDisabled("FILTER");
                ImGui::Separator();
                const char *filterLabels[] = {"All types", "Models", "Materials", "Textures", "Shaders", "Fonts", "Other"};
                for (int i = 0; i < 7; i++) {
                    const FilterKind fk = static_cast<FilterKind>(i);
                    bool active = (filterKind == fk);
                    if (active) ImGui::PushStyleColor(ImGuiCol_Text, EditorTheme::color(EditorTheme::Color::AccentHover));
                    if (ImGui::MenuItem(filterLabels[i])) {
                        filterKind = fk;
                        filterMenuOpen = false;
                    }
                    if (active) ImGui::PopStyleColor();
                }
                ImGui::EndPopup();
            }
            ImGui::OpenPopup("##ae_filterpop");
        }

        // Separator line
        ImGui::SameLine(0, 6); {
            ImVec2 p = ImGui::GetCursorScreenPos();
            AEC::separatorLine(dl, ImVec2(p.x, p.y - 2), ImVec2(p.x, p.y + 22));
        }
        ImGui::Dummy(ImVec2(1, 26));
        ImGui::SameLine(0, 6);

        // View mode buttons
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 4));

        const char *viewIconNames[] = {"layout-grid", "list", "layout-detail"};
        const char *viewIds[]       = {"##ae_vm0", "##ae_vm1", "##ae_vm2"};
        const char *viewTips[]      = {"Grid view", "List view", "Detail view"};
        for (int i = 0; i < 3; i++) {
            const ViewMode vm = static_cast<ViewMode>(i);
            const bool active = (viewMode == vm);
            const auto &ic = iconRegistry.get(viewIconNames[i], 16);
            bool clicked = ic.valid()
                ? AEC::toolbarIconButton(viewIds[i], ic.textureId(), ic.size(), viewTips[i], active, ImVec2(btnW, 0))
                : AEC::toolbarButton(viewIds[i], viewTips[i], active, ImVec2(btnW, 0));
            if (clicked) viewMode = vm;
            if (i < 2) ImGui::SameLine(0, 2);
        }

        ImGui::PopStyleVar();

        ImGui::SameLine(0, 4);

        // Import button
        if (AEC::toolbarButton("+##ae_import", "Import asset", false, ImVec2(btnW, 0)))
            ImGui::OpenPopup("##ae_importpop");

        ImGui::PopStyleVar(2);

        // Bottom border line
        ImVec2 borderY = ImVec2(toolbarMin.x, toolbarMin.y + toolbarH);
        AEC::separatorLine(dl, borderY, ImVec2(borderY.x + winSize.x, borderY.y));

        // Advance cursor past toolbar
        ImGui::SetCursorScreenPos(ImVec2(toolbarMin.x, toolbarMin.y + toolbarH + 1));
    }

    // ── folder tree ──────────────────────────────────────────────────────────

    void AssetExplorerPanel::renderFolderTree(const std::filesystem::path &assetRoot) {
        ImGuiTreeNodeFlags rootFlags = ImGuiTreeNodeFlags_OpenOnArrow |
                                       ImGuiTreeNodeFlags_OpenOnDoubleClick |
                                       ImGuiTreeNodeFlags_DefaultOpen |
                                       ImGuiTreeNodeFlags_SpanFullWidth;
        if (currentDirectory == assetRoot) rootFlags |= ImGuiTreeNodeFlags_Selected;

        bool open = ImGui::TreeNodeEx("Assets", rootFlags);
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            currentDirectory = assetRoot;
            selectedPath.clear();
            requestAssetRefresh();
        }
        if (open) {
            if (assetSnapshot.valid && assetSnapshot.assetRoot == assetRoot) {
                for (const FolderNode &child: assetSnapshot.folderTree.children) {
                    renderFolderNode(child);
                }
            }
            ImGui::TreePop();
        }
    }

    void AssetExplorerPanel::renderFolderNode(const FolderNode &node) {
        const bool selected = (currentDirectory == node.path);

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                   ImGuiTreeNodeFlags_OpenOnDoubleClick |
                                   ImGuiTreeNodeFlags_SpanFullWidth;
        if (selected) flags |= ImGuiTreeNodeFlags_Selected;
        if (node.children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;

        // Draw amber selection indicator bar
        if (selected) {
            ImDrawList *dl = ImGui::GetWindowDrawList();
            ImVec2 p = ImGui::GetCursorScreenPos();
            dl->AddRectFilled(ImVec2(p.x - 4, p.y + 1),
                              ImVec2(p.x - 1, p.y + ImGui::GetTextLineHeight() - 1),
                              AEC::color(AEC::Color::SelectionBar), 1.0f);
        }

        const bool nodeOpen = ImGui::TreeNodeEx(node.name.c_str(), flags);

        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            currentDirectory = node.path;
            selectedPath.clear();
            requestAssetRefresh();
        }

        if (nodeOpen) {
            for (const FolderNode &child: node.children) {
                renderFolderNode(child);
            }
            ImGui::TreePop();
        }
    }

    // ── grid view ────────────────────────────────────────────────────────────

    void AssetExplorerPanel::renderTileGrid(const std::vector<AssetEntry> &entries,
                                            const std::filesystem::path &assetRoot) {
        const float panelW = ImGui::GetContentRegionAvail().x;

        // OPT: hoist all metric calls outside the loop
        const float tileSize = AEC::tileMin();
        const float tileGap  = AEC::tileGap();
        const float labelH   = AEC::tileLabelHeight();

        const int   cols  = std::max(1, static_cast<int>(panelW / (tileSize + tileGap)));
        const float tile  = std::floor((panelW - tileGap * static_cast<float>(cols - 1)) /
                                       static_cast<float>(cols));
        const float rowH  = tile + labelH + tileGap;
        const float cellW = tile + tileGap;

        const ImVec2 origin = ImGui::GetCursorScreenPos();

        // OPT: cull off-screen tiles — only submit InvisibleButton + drawTile for
        //      rows that are actually in the visible scroll region.
        const float scrollY  = ImGui::GetScrollY();
        const float visibleH = ImGui::GetContentRegionAvail().y;
        const float visMin   = scrollY - rowH;           // one extra row buffer
        const float visMax   = scrollY + visibleH + rowH;

        const int totalEntries = static_cast<int>(entries.size());

        for (int idx = 0; idx < totalEntries; ++idx) {
            const int   row  = idx / cols;
            const int   col  = idx % cols;
            const float tileY = static_cast<float>(row) * rowH;

            // Skip tiles that are above or below the visible area entirely.
            // We still increment idx so layout math stays correct.
            if (tileY < visMin || tileY > visMax) {
                continue;
            }

            const ImVec2 tileMin(
                origin.x + static_cast<float>(col) * cellW,
                origin.y + tileY
            );

            const AssetEntry &entry = entries[idx];

            ImGui::SetCursorScreenPos(tileMin);
            // OPT: use integer index as ID — avoids path.string() heap allocation per tile
            ImGui::PushID(idx);

            const bool selected = (selectedPath == entry.path);
            ImGui::InvisibleButton("##t", ImVec2(tile, tile + labelH));
            const bool hovered = ImGui::IsItemHovered();

            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                selectedPath = entry.path;
                if (entry.directory && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    currentDirectory = entry.path;
                    selectedPath.clear();
                    requestAssetRefresh();
                }
                if (!entry.directory && isTextureKind(entry.kind) &&
                    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    previewTexturePath = entry.path;
                    texturePreviewOpen = true;
                    previewZoom = 1.0f;
                }
            }

            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                selectedPath = entry.path;
                contextMenuPath = entry.path;
                contextMenuOpen = true;
                ImGui::OpenPopup("##ae_ctx");
            }

            drawTile(entry, tileMin, tile, selected, hovered, assetRoot);

            if (hovered) {
                ImGui::SetTooltip("%s\n%s", kindName(entry.kind),
                                  displayPath(entry.relativePath).c_str());
            }

            ImGui::PopID();
        }

        // Always set the full dummy so the scrollbar reflects the real content height,
        // even though we skipped rendering off-screen tiles above.
        if (totalEntries > 0) {
            const int rows = (totalEntries + cols - 1) / cols;
            ImGui::SetCursorScreenPos(origin);
            ImGui::Dummy(ImVec2(panelW, static_cast<float>(rows) * rowH));
        }
    }

    void AssetExplorerPanel::drawTile(const AssetEntry &entry, const ImVec2 &min, float size,
                                      bool selected, bool hovered,
                                      const std::filesystem::path &assetRoot) {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const ImVec2 thumbMax(min.x + size, min.y + size);

        // Thumbnail background
        // OPT: kindColorBg is now a static lookup — no per-tile float math
        const ImU32 bgTint = kindColorBg(entry.kind);
        AEC::tileBackground(dl, min, thumbMax, bgTint, selected, hovered);

        // OPT: only draw the decorative grid on hovered/selected tiles;
        //      otherwise it generates ~24 AddLine calls per tile for pure decoration.
        if (hovered || selected) {
            AEC::thumbnailGrid(dl, min, thumbMax);
        }

        // Actual thumbnail (texture preview) or type icon placeholder
        if (entry.directory) {
            // Folder icon
            AEC::folderIcon(dl, min, thumbMax, kindColor(entry.kind));
        } else {
            const bool textureDrawn = drawTexturePreview(entry, min, thumbMax, assetRoot);
            if (!textureDrawn) {
                const ImU32 iconColor = kindColor(entry.kind);
                if (entry.kind == AssetKind::Model) {
                    AEC::cubeIcon(dl, min, thumbMax, iconColor);
                } else if (entry.kind == AssetKind::Material) {
                    AEC::materialIcon(dl, min, thumbMax, iconColor);
                } else if (entry.kind == AssetKind::Other) {
                    AEC::fileIcon(dl, min, thumbMax, iconColor);
                } else {
                    const char *badge = kindBadge(entry.kind);
                    ImVec2 ts = ImGui::CalcTextSize(badge);
                    dl->AddText(nullptr, size * 0.18f,
                                ImVec2(min.x + (size - ts.x * (size * 0.18f / ImGui::GetFontSize())) * 0.5f,
                                       min.y + size * 0.38f),
                                iconColor, badge);
                }
            }
        }

        // Corner chip (top-left)
        if (!entry.directory) {
            const std::string modelExtension = entry.kind == AssetKind::Model ? extensionBadge(entry.path) : std::string();
            const char *label = modelExtension.empty() ? kindBadge(entry.kind) : modelExtension.c_str();
            AEC::badge(dl, min, label, kindColor(entry.kind));
        }

        // Label below thumbnail
        const ImVec2 lblMin(min.x + 2, thumbMax.y + 5);
        const ImU32 lblColor = selected ? AEC::color(AEC::Color::Text) : AEC::color(AEC::Color::TextDim);
        ImGui::PushClipRect(lblMin, ImVec2(lblMin.x + size - 4, lblMin.y + AEC::tileLabelHeight()), true);
        AEC::textEllipsis(dl, lblMin, size - 4.0f, entry.name.c_str(), lblColor);
        ImGui::PopClipRect();
    }

    // ── list view ────────────────────────────────────────────────────────────

    void AssetExplorerPanel::renderListView(const std::vector<AssetEntry> &entries,
                                            const std::filesystem::path &assetRoot) {
        const ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
                                      ImGuiTableFlags_SizingFixedFit;

        // Column header
        ImGui::PushStyleColor(ImGuiCol_TableBorderLight, AEC::colorVec4(AEC::Color::Separator));
        if (!ImGui::BeginTable("##ae_list", 4, flags)) {
            ImGui::PopStyleColor();
            return;
        }

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 86.0f);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 72.0f);
        ImGui::TableSetupColumn("Modified", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableHeadersRow();

        // OPT: cache draw list pointer outside the loop
        ImDrawList *dl = ImGui::GetWindowDrawList();

        for (int idx = 0; idx < static_cast<int>(entries.size()); ++idx) {
            const AssetEntry &entry = entries[idx];

            ImGui::TableNextRow(0, 28.0f);
            ImGui::TableSetColumnIndex(0);

            const bool selected = (selectedPath == entry.path);
            // OPT: integer index ID — no path.string() allocation
            ImGui::PushID(idx);

            ImVec2 rowMin = ImGui::GetCursorScreenPos();
            ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0, 0.5f));
            if (ImGui::Selectable("##lrow", selected,
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                                  ImVec2(0, 24))) {
                selectedPath = entry.path;
                if (entry.directory) {
                    currentDirectory = entry.path;
                    requestAssetRefresh();
                }
                if (!entry.directory && isTextureKind(entry.kind) &&
                    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    previewTexturePath = entry.path;
                    texturePreviewOpen = true;
                    previewZoom = 1.0f;
                }
            }
            ImGui::PopStyleVar();

            // Selection highlight
            if (selected) {
                dl->AddRectFilled(rowMin, ImVec2(rowMin.x + 9999, rowMin.y + 26), AEC::color(AEC::Color::SelectionSoft));
                dl->AddRect(rowMin, ImVec2(rowMin.x + 9999, rowMin.y + 26), AEC::color(AEC::Color::SelectionRing), 0, 0, 0.8f);
            }

            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                selectedPath = entry.path;
                contextMenuPath = entry.path;
                contextMenuOpen = true;
                ImGui::OpenPopup("##ae_ctx");
            }

            // Overlay icon + name
            ImGui::SameLine();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4); {
                ImVec2 ic = ImGui::GetCursorScreenPos();
                const float is = 15.0f;
                const ImU32 ic_col = entry.directory
                                         ? kindColor(AssetKind::Folder)
                                         : kindColor(entry.kind);

                const ImVec2 iconMax(ic.x + is, ic.y + is);
                dl->AddRectFilled(ic, iconMax, AEC::withAlpha(ic_col, 0.24f), 2.0f);
                if (entry.kind == AssetKind::Model) {
                    AEC::cubeIcon(dl, ic, iconMax, ic_col);
                } else if (entry.kind == AssetKind::Material) {
                    AEC::materialIcon(dl, ic, iconMax, ic_col);
                } else if (entry.kind == AssetKind::Other) {
                    AEC::fileIcon(dl, ic, iconMax, ic_col);
                } else {
                    const char *bl = entry.directory ? "D" : kindBadge(entry.kind);
                    ImVec2 ts = ImGui::CalcTextSize(bl);
                    dl->AddText(ImVec2(ic.x + (is - ts.x) * 0.5f, ic.y + (is - ts.y) * 0.5f),
                                AEC::color(AEC::Color::BadgeText), bl);
                }
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + is + 6);
            }

            ImGui::TextUnformatted(entry.name.c_str());

            ImGui::TableSetColumnIndex(1);
            if (!entry.directory) {
                const ImU32 bc = kindColor(entry.kind);
                const char *bl = kindBadge(entry.kind);
                ImVec2 ts = ImGui::CalcTextSize(bl);
                const float bw = ts.x + 12.0f;
                const float bh = 18.0f;
                ImVec2 bp = ImGui::GetCursorScreenPos();
                bp.y += 3;
                dl->AddRectFilled(bp, ImVec2(bp.x + bw, bp.y + bh), bc, 9.0f);
                dl->AddText(ImVec2(bp.x + 6, bp.y + 2), AEC::color(AEC::Color::BadgeText), bl);
                ImGui::Dummy(ImVec2(bw, bh));
            }

            ImGui::TableSetColumnIndex(2);
            if (!entry.directory)
                ImGui::TextDisabled("%s", entry.formattedSize.c_str());

            ImGui::TableSetColumnIndex(3);
            if (!entry.directory)
                ImGui::TextDisabled("%s", entry.formattedDate.c_str());

            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", displayPath(entry.relativePath).c_str());

            ImGui::PopID();
        }

        ImGui::EndTable();
        ImGui::PopStyleColor();
    }

    // ── detail view ──────────────────────────────────────────────────────────

    void AssetExplorerPanel::renderDetailView(const std::vector<AssetEntry> &entries,
                                              const std::filesystem::path &assetRoot) {
        const float rowH = 60.0f;
        const float thumbS = 44.0f;
        const float panelW = ImGui::GetContentRegionAvail().x;

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 6));

        // OPT: cache draw list outside the loop
        ImDrawList *dl = ImGui::GetWindowDrawList();

        for (int idx = 0; idx < static_cast<int>(entries.size()); ++idx) {
            const AssetEntry &entry = entries[idx];

            // OPT: integer index ID
            ImGui::PushID(idx);
            const bool selected = (selectedPath == entry.path);

            ImVec2 rowMin = ImGui::GetCursorScreenPos();

            // Row background
            const ImU32 bgColor = selected ? AEC::color(AEC::Color::SelectionSoft) : AEC::color(AEC::Color::DetailRow);
            dl->AddRectFilled(rowMin, ImVec2(rowMin.x + panelW, rowMin.y + rowH), bgColor, AEC::rowRounding());
            if (selected)
                dl->AddRect(rowMin, ImVec2(rowMin.x + panelW, rowMin.y + rowH), AEC::color(AEC::Color::SelectionRing), AEC::rowRounding(), 0, 1.2f);
            else
                dl->AddRect(rowMin, ImVec2(rowMin.x + panelW, rowMin.y + rowH),
                            AEC::color(AEC::Color::Separator, 0.5f), AEC::rowRounding(), 0, 1.0f);

            // Invisible hit target
            ImGui::InvisibleButton("##dr", ImVec2(panelW, rowH));
            if (ImGui::IsItemHovered() && !selected) {
                dl->AddRectFilled(rowMin, ImVec2(rowMin.x + panelW, rowMin.y + rowH),
                                  AEC::color(AEC::Color::TileHover, 0.45f), AEC::rowRounding());
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                selectedPath = entry.path;
                if (entry.directory) {
                    currentDirectory = entry.path;
                    requestAssetRefresh();
                }
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                selectedPath = entry.path;
                contextMenuPath = entry.path;
                contextMenuOpen = true;
                ImGui::OpenPopup("##ae_ctx");
            }

            // Thumbnail (44x44)
            const ImVec2 tmin(rowMin.x + 10, rowMin.y + (rowH - thumbS) * 0.5f);
            const ImVec2 tmax(tmin.x + thumbS, tmin.y + thumbS); {
                dl->AddRectFilled(tmin, tmax, kindColorBg(entry.kind), 5.0f);
                if (entry.directory) {
                    AEC::folderIcon(dl, tmin, tmax, kindColor(AssetKind::Folder));
                } else {
                    const bool drawn = drawTexturePreview(entry, tmin, tmax, assetRoot);
                    if (!drawn) {
                        if (entry.kind == AssetKind::Model) {
                            AEC::cubeIcon(dl, tmin, tmax, kindColor(entry.kind));
                        } else if (entry.kind == AssetKind::Material) {
                            AEC::materialIcon(dl, tmin, tmax, kindColor(entry.kind));
                        } else if (entry.kind == AssetKind::Other) {
                            AEC::fileIcon(dl, tmin, tmax, kindColor(entry.kind));
                        } else {
                            const char *bl = kindBadge(entry.kind);
                            ImVec2 ts = ImGui::CalcTextSize(bl);
                            dl->AddText(ImVec2(tmin.x + (thumbS - ts.x) * 0.5f,
                                               tmin.y + (thumbS - ts.y) * 0.5f),
                                        kindColor(entry.kind), bl);
                        }
                    }
                }
                dl->AddRect(tmin, tmax, AEC::color(AEC::Color::TileBorder, 0.8f), 5.0f, 0, 1.0f);
            }

            // Name + type badge
            const float nameX = tmax.x + 12;
            const float nameY = rowMin.y + 10;
            dl->AddText(nullptr, 0, ImVec2(nameX, nameY), AEC::color(AEC::Color::Text), entry.name.c_str(), nullptr);

            if (!entry.directory) {
                // Type pill badge
                const char *bl = kindBadge(entry.kind);
                ImVec2 ts = ImGui::CalcTextSize(bl);
                const float bw = ts.x + 10.0f;
                const float bh = 16.0f;
                const float by = nameY + ImGui::GetTextLineHeight() + 4;
                dl->AddRectFilled(ImVec2(nameX, by), ImVec2(nameX + bw, by + bh), kindColor(entry.kind), 8.0f);
                dl->AddText(ImVec2(nameX + 5, by + 2), AEC::color(AEC::Color::BadgeText), bl);

                float metaX = rowMin.x + panelW - 10;
                // Modified
                if (!entry.formattedDate.empty()) {
                    ImVec2 vs = ImGui::CalcTextSize(entry.formattedDate.c_str());
                    metaX -= vs.x + 2;
                    dl->AddText(ImVec2(metaX, rowMin.y + 12 + 13), AEC::color(AEC::Color::TextDim),
                                entry.formattedDate.c_str());
                    ImVec2 ls = ImGui::CalcTextSize("MODIFIED");
                    dl->AddText(nullptr, ls.y * 0.8f,
                                ImVec2(metaX, rowMin.y + 12), AEC::color(AEC::Color::TextDim), "MODIFIED", nullptr);
                    metaX -= 24;
                }
                // Size
                if (!entry.formattedSize.empty()) {
                    ImVec2 vs = ImGui::CalcTextSize(entry.formattedSize.c_str());
                    metaX -= vs.x + 2;
                    dl->AddText(ImVec2(metaX, rowMin.y + 12 + 13), AEC::color(AEC::Color::TextDim),
                                entry.formattedSize.c_str());
                    ImVec2 ls = ImGui::CalcTextSize("SIZE");
                    dl->AddText(nullptr, ls.y * 0.8f,
                                ImVec2(metaX, rowMin.y + 12), AEC::color(AEC::Color::TextDim), "SIZE", nullptr);
                }
            }

            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", displayPath(entry.relativePath).c_str());

            ImGui::PopID();
        }

        ImGui::PopStyleVar();
    }

    // ── detail strip ─────────────────────────────────────────────────────────

    void AssetExplorerPanel::renderDetailStrip(const std::vector<AssetEntry> &entries) {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        ImVec2 strip = ImGui::GetCursorScreenPos();
        float winW = ImGui::GetWindowWidth();

        // Background
        dl->AddRectFilled(strip, ImVec2(strip.x + winW, strip.y + AEC::stripHeight()), AEC::color(AEC::Color::Strip));
        AEC::separatorLine(dl, strip, ImVec2(strip.x + winW, strip.y));

        ImGui::BeginChild("##ae_strip", ImVec2(0, AEC::stripHeight()), false, ImGuiWindowFlags_NoScrollbar);

        const AssetEntry *sel = findSelected(entries);

        ImGui::SetCursorPos(ImVec2(14, (AEC::stripHeight() - ImGui::GetTextLineHeight()) * 0.5f));

        if (!sel) {
            // Count summary
            int folders = 0, files = 0;
            uintmax_t total = 0;
            for (const auto &e: entries) {
                if (e.directory) ++folders;
                else {
                    ++files;
                    total += e.bytes;
                }
            }
            ImGui::TextDisabled("%d folder%s, %d item%s", folders, folders != 1 ? "s" : "",
                                files, files != 1 ? "s" : "");
            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 80);
            ImGui::TextDisabled("%s", formatSize(total).c_str());
        } else {
            // Selected asset info
            const float thumbS = 28.0f;
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImVec2 tmin(p.x, p.y - 2);
            ImVec2 tmax(tmin.x + thumbS, tmin.y + thumbS);
            dl->AddRectFilled(tmin, tmax, kindColorBg(sel->kind), 4.0f);
            const char *bl = kindBadge(sel->kind);
            ImVec2 ts = ImGui::CalcTextSize(bl);
            if (sel->kind == AssetKind::Model) {
                AEC::cubeIcon(dl, tmin, tmax, kindColor(sel->kind));
            } else if (sel->kind == AssetKind::Material) {
                AEC::materialIcon(dl, tmin, tmax, kindColor(sel->kind));
            } else if (sel->kind == AssetKind::Other) {
                AEC::fileIcon(dl, tmin, tmax, kindColor(sel->kind));
            } else {
                dl->AddText(ImVec2(tmin.x + (thumbS - ts.x) * 0.5f, tmin.y + (thumbS - ts.y) * 0.5f),
                            kindColor(sel->kind), bl);
            }
            dl->AddRect(tmin, tmax, AEC::color(AEC::Color::TileBorder, 0.65f), 4.0f, 0, 1.0f);

            ImGui::Dummy(ImVec2(thumbS + 8, 0));
            ImGui::SameLine();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 3);

            ImGui::BeginGroup();
            ImGui::Text("%s", sel->name.c_str());
            ImGui::SameLine(0, 8);
            // Pill badge
            {
                ImVec2 bp = ImGui::GetCursorScreenPos();
                bp.y += 1;
                const float bw = ts.x + 10;
                const float bh = 16;
                dl->AddRectFilled(bp, ImVec2(bp.x + bw, bp.y + bh), kindColor(sel->kind), 8.0f);
                dl->AddText(ImVec2(bp.x + 5, bp.y + 2), AEC::color(AEC::Color::BadgeText), bl);
                ImGui::Dummy(ImVec2(bw, bh));
            }
            ImGui::EndGroup();

            // Metadata (right side)
            float mx = ImGui::GetWindowWidth() - 14;
            auto metaCell = [&](const char *label, const char *value, float &x) {
                ImVec2 vs = ImGui::CalcTextSize(value);
                ImVec2 ls = ImGui::CalcTextSize(label);
                float w = std::max(vs.x, ls.x);
                x -= w;
                ImVec2 sy = ImGui::GetCursorScreenPos();
                const float cy = sy.y - 3;
                dl->AddText(nullptr, ls.y * 0.78f, ImVec2(x, cy), AEC::color(AEC::Color::TextDim), label, nullptr);
                dl->AddText(ImVec2(x, cy + 13), AEC::color(AEC::Color::Text), value);
                x -= 20;
            };

            if (!sel->formattedDate.empty()) metaCell("MODIFIED", sel->formattedDate.c_str(), mx);
            if (!sel->formattedSize.empty()) metaCell("SIZE", sel->formattedSize.c_str(), mx);
            if (sel->kind != AssetKind::Folder)
                metaCell("TYPE", kindName(sel->kind), mx);
        }

        ImGui::EndChild();
    }

    // ── context menu ─────────────────────────────────────────────────────────

    void AssetExplorerPanel::renderContextMenu(const std::filesystem::path &assetRoot) {
        if (!contextMenuOpen) return;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, EditorTheme::color(EditorTheme::Color::PopupBg));

        if (ImGui::BeginPopup("##ae_ctx")) {
            const std::string name = contextMenuPath.filename().string();
            ImGui::TextDisabled("%s", name.c_str());
            ImGui::Separator();

            if (ImGui::MenuItem("Open", "Enter")) {
            }
            if (ImGui::MenuItem("Rename", "F2")) {
            }
            if (ImGui::MenuItem("Duplicate", "Ctrl+D")) {
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Reimport")) {
            }
            if (ImGui::MenuItem("Reveal in Explorer")) {
                // Shell reveal
            }
            if (ImGui::MenuItem("Copy Path")) {
            }
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, EditorTheme::color(EditorTheme::Color::AxisX));
            if (ImGui::MenuItem("Delete", "Del")) {
            }
            ImGui::PopStyleColor();

            ImGui::EndPopup();
        } else {
            contextMenuOpen = false;
        }

        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }

    // ── texture preview window ────────────────────────────────────────────────

    void AssetExplorerPanel::renderTexturePreviewWindow(const std::filesystem::path &assetRoot) {
        if (!texturePreviewOpen) return;
        if (previewTexturePath.empty() || !std::filesystem::exists(previewTexturePath)) {
            texturePreviewOpen = false;
            previewTexturePath.clear();
            return;
        }

        std::string title = previewTexturePath.filename().string() + "###TexPreview";
        ImGui::SetNextWindowSize(ImVec2(720.0f, 520.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(title.c_str(), &texturePreviewOpen)) {
            ImGui::End();
            return;
        }

        VkDescriptorSet descriptor = VK_NULL_HANDLE;
        Texture *texture = nullptr;
        if (!ensureTexturePreview(previewTexturePath, assetRoot, descriptor, texture) ||
            descriptor == VK_NULL_HANDLE || !texture) {
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

        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float tw = static_cast<float>(std::max(1u, texture->width()));
        const float th = static_cast<float>(std::max(1u, texture->height()));
        const float scale = std::min(avail.x / tw, avail.y / th) * previewZoom;
        const ImVec2 imgSz(std::max(1.0f, std::floor(tw * scale)),
                           std::max(1.0f, std::floor(th * scale)));
        const float cx = ImGui::GetCursorPosX() + std::max(0.0f, (avail.x - imgSz.x) * 0.5f);
        ImGui::SetCursorPosX(cx);
        ImGui::Image((ImTextureID) descriptor, imgSz);

        ImGui::End();
    }

    bool AssetExplorerPanel::drawTexturePreview(const AssetEntry &entry, const ImVec2 &min, const ImVec2 &max,
                                                const std::filesystem::path &assetRoot) {
        if (!isTextureKind(entry.kind)) return false;

        VkDescriptorSet descriptor = VK_NULL_HANDLE;
        Texture *texture = nullptr;
        if (!ensureTexturePreview(entry.path, assetRoot, descriptor, texture) ||
            descriptor == VK_NULL_HANDLE)
            return false;

        ImVec2 imgMin = min, imgMax = max;
        if (texture && texture->width() > 0 && texture->height() > 0) {
            const float aw = max.x - min.x;
            const float ah = max.y - min.y;
            const float aspect = static_cast<float>(texture->width()) /
                                 static_cast<float>(texture->height());
            float w = aw, h = w / aspect;
            if (h > ah) {
                h = ah;
                w = h * aspect;
            }
            imgMin.x = min.x + (aw - w) * 0.5f;
            imgMin.y = min.y + (ah - h) * 0.5f;
            imgMax.x = imgMin.x + w;
            imgMax.y = imgMin.y + h;
        }

        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(min, max, AEC::color(AEC::Color::TileBg), 4.0f);
        dl->AddImage((ImTextureID) descriptor, imgMin, imgMax);
        return true;
    }

    bool AssetExplorerPanel::ensureTexturePreview(const std::filesystem::path &path,
                                                  const std::filesystem::path &assetRoot,
                                                  VkDescriptorSet &descriptor, Texture *&texture) {
        const std::string virtualPath = displayPath(relativeTo(path, assetRoot));
        TexturePreview &preview = texturePreviews[virtualPath];
        if (!preview.handle.valid() && !preview.loadFailed) {
            try {
                preview.handle = projectLayer.assetManager().store<Texture>(virtualPath);
            } catch (const std::exception &e) {
                preview.loadFailed = true;
                AT_WARN("AssetExplorerPanel: texture preview load failed '{}': {}", virtualPath, e.what());
            }
        }
        if (!preview.handle.valid() || !preview.handle.isReady()) return false;
        if (preview.descriptor == VK_NULL_HANDLE) {
            const VkDescriptorImageInfo info = preview.handle.descriptor();
            preview.descriptor = ImGuiLayer::addTexture(info.sampler, info.imageView, info.imageLayout);
        }
        descriptor = preview.descriptor;
        texture = preview.handle.get();
        return descriptor != VK_NULL_HANDLE && texture != nullptr;
    }

    // ── data helpers ─────────────────────────────────────────────────────────

    void AssetExplorerPanel::syncProjectRoot() {
        const std::filesystem::path assetRoot = projectLayer.project().assetsPath();
        if (assetRoot == cachedAssetRoot) return;
        cachedAssetRoot = assetRoot;
        currentDirectory = cachedAssetRoot;
        selectedPath.clear();
        contextMenuPath.clear();
        previewTexturePath.clear();
        searchText.clear();
        texturePreviewOpen = false;
        previewZoom = 1.0f;
        clearPreviewCache();
        assetSnapshot = {};
        cachedFilteredEntries.clear();
        cachedFilterGeneration = 0;
        requestAssetRefresh();
        requestTreeRefresh();
    }

    void AssetExplorerPanel::clearPreviewCache() {
        for (auto &[_, p]: texturePreviews) {
            if (p.descriptor != VK_NULL_HANDLE) {
                ImGuiLayer::removeTexture(p.descriptor);
                p.descriptor = VK_NULL_HANDLE;
            }
        }
        texturePreviews.clear();
    }

    void AssetExplorerPanel::requestAssetRefresh() {
        assetRefreshRequested = true;
    }

    void AssetExplorerPanel::requestTreeRefresh() {
        treeRefreshRequested = true;
    }

    void AssetExplorerPanel::pollAssetRefresh() {
        if (!assetRefreshFuture.valid()) return;
        if (assetRefreshFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;

        try {
            AssetSnapshot snapshot = assetRefreshFuture.get();
            if (snapshot.generation >= appliedRefreshGeneration) {
                appliedRefreshGeneration = snapshot.generation;
                // Preserve the current tree in the snapshot if we didn't rebuild it
                if (snapshot.treeGeneration == 0 && assetSnapshot.valid) {
                    snapshot.folderTree   = std::move(assetSnapshot.folderTree);
                    snapshot.treeGeneration = assetSnapshot.treeGeneration;
                }
                assetSnapshot = std::move(snapshot);
                // Invalidate the filter cache so it rebuilds on the next frame
                cachedFilterGeneration = 0;
            }
        } catch (const std::exception &e) {
            AT_WARN("AssetExplorerPanel: asset refresh failed: {}", e.what());
        }
    }

    // OPT: poll the tree future separately — it runs on a much longer interval
    //      so it won't interfere with the entries refresh cadence.
    void AssetExplorerPanel::pollTreeRefresh() {
        if (!treeRefreshFuture.valid()) return;
        if (treeRefreshFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;

        try {
            FolderNode tree = treeRefreshFuture.get();
            appliedTreeGeneration = requestedTreeGeneration;
            assetSnapshot.folderTree   = std::move(tree);
            assetSnapshot.treeGeneration = appliedTreeGeneration;
        } catch (const std::exception &e) {
            AT_WARN("AssetExplorerPanel: tree refresh failed: {}", e.what());
        }
    }

    void AssetExplorerPanel::scheduleAssetRefreshIfNeeded() {
        if (cachedAssetRoot.empty() || currentDirectory.empty()) return;
        // Don't schedule if a refresh is already in flight
        if (assetRefreshFuture.valid()) return;

        const auto now = std::chrono::steady_clock::now();
        const bool intervalElapsed = lastAssetRefresh == std::chrono::steady_clock::time_point{} ||
                                     now - lastAssetRefresh >= AssetRefreshInterval;
        const bool snapshotMismatch =
                !assetSnapshot.valid ||
                assetSnapshot.assetRoot != cachedAssetRoot ||
                assetSnapshot.directory != currentDirectory;

        if (!assetRefreshRequested && !intervalElapsed && !snapshotMismatch) return;

        assetRefreshRequested = false;
        lastAssetRefresh = now;

        const uint64_t generation    = ++requestedRefreshGeneration;
        const uint64_t treeGen       = appliedTreeGeneration;
        const std::filesystem::path assetRoot  = cachedAssetRoot;
        const std::filesystem::path directory  = currentDirectory;

        // OPT: pass the existing tree so the entries-only refresh can reuse it
        //      without doing the expensive recursive scan again.
        FolderNode existingTree = assetSnapshot.valid ? assetSnapshot.folderTree : FolderNode{};

        assetRefreshFuture = projectLayer.getRenderer().device().executor().submit(
            [assetRoot, directory, generation, treeGen, existingTree = std::move(existingTree)]() mutable {
                ATLAS_PROFILE_SCOPE("AssetExplorerPanel::buildAssetSnapshot");
                return AssetExplorerPanel::buildAssetSnapshot(
                    std::move(assetRoot), std::move(directory), generation, treeGen, std::move(existingTree));
            });
    }

    // OPT: tree refresh is on its own slower cadence (TreeRefreshInterval).
    //      It only does the recursive scan and nothing else.
    void AssetExplorerPanel::scheduleTreeRefreshIfNeeded() {
        if (cachedAssetRoot.empty()) return;
        // Don't schedule if a tree rebuild is already in flight
        if (treeRefreshFuture.valid()) return;
        // Also wait until the entries future is done to avoid racing on assetSnapshot
        if (assetRefreshFuture.valid()) return;

        const auto now = std::chrono::steady_clock::now();
        const bool intervalElapsed = lastTreeRefresh == std::chrono::steady_clock::time_point{} ||
                                     now - lastTreeRefresh >= TreeRefreshInterval;

        if (!treeRefreshRequested && !intervalElapsed) return;

        treeRefreshRequested = false;
        lastTreeRefresh = now;
        ++requestedTreeGeneration;

        const std::filesystem::path assetRoot = cachedAssetRoot;

        treeRefreshFuture = projectLayer.getRenderer().device().executor().submit(
            [assetRoot]() {
                ATLAS_PROFILE_SCOPE("AssetExplorerPanel::buildFolderTree");
                return AssetExplorerPanel::buildFolderTree(assetRoot);
            });
    }

    // OPT: buildAssetSnapshot no longer unconditionally calls buildFolderTree.
    //      It receives the existing tree and embeds it as-is, so entries refresh
    //      is only a flat directory_iterator call (very fast).
    AssetExplorerPanel::AssetSnapshot AssetExplorerPanel::buildAssetSnapshot(
        std::filesystem::path assetRoot, std::filesystem::path directory,
        const uint64_t generation, const uint64_t treeGeneration, FolderNode existingTree) {
        AssetSnapshot snapshot;
        snapshot.assetRoot     = std::move(assetRoot);
        snapshot.directory     = std::move(directory);
        snapshot.generation    = generation;
        snapshot.treeGeneration = treeGeneration;
        snapshot.entries       = collectEntries(snapshot.directory, snapshot.assetRoot);
        snapshot.folderTree    = std::move(existingTree);
        snapshot.valid         = true;
        return snapshot;
    }

    AssetExplorerPanel::FolderNode AssetExplorerPanel::buildFolderTree(const std::filesystem::path &directory) {
        FolderNode node;
        node.path = directory;
        node.name = directory.filename().string();
        if (node.name.empty()) {
            node.name = directory.string();
        }

        std::error_code err;
        for (const auto &entry: std::filesystem::directory_iterator(directory, err)) {
            if (entry.is_directory(err)) {
                node.children.push_back(buildFolderTree(entry.path()));
            }
        }

        std::ranges::sort(node.children, [](const FolderNode &lhs, const FolderNode &rhs) {
            return lower(lhs.name) < lower(rhs.name);
        });

        return node;
    }

    std::vector<AssetExplorerPanel::AssetEntry> AssetExplorerPanel::collectEntries(
        const std::filesystem::path &directory, const std::filesystem::path &assetRoot) {
        std::vector<AssetEntry> entries;
        std::error_code err;
        if (directory.empty() || !std::filesystem::exists(directory, err)) return entries;

        for (const auto &e: std::filesystem::directory_iterator(directory, err)) {
            const bool isDir = e.is_directory(err);
            AssetEntry ae{};
            ae.path = e.path();
            ae.relativePath = relativeTo(ae.path, assetRoot);
            ae.name = ae.path.filename().string();
            // OPT: pre-compute lowercased name once here so applyFiltersAndSort
            //      never has to allocate a string per entry per frame.
            ae.nameLower = lower(ae.name);
            ae.directory = isDir;
            ae.kind = classify(ae.path, isDir);

            if (!isDir) {
                std::error_code se;
                ae.bytes = std::filesystem::file_size(ae.path, se);
                ae.formattedSize = formatSize(ae.bytes);

                std::error_code te;
                ae.lastWriteTime = std::filesystem::last_write_time(ae.path, te);
                if (!te) ae.formattedDate = formatDate(ae.lastWriteTime);
            }

            entries.push_back(std::move(ae));
        }
        return entries;
    }

    // OPT: filteredEntries() replaces the old per-frame applyFiltersAndSort() call.
    //      It checks whether any of the inputs have changed and only rebuilds the
    //      cached result when they actually have.  On a typical idle frame with no
    //      user interaction this is a handful of comparisons and an early return.
    const std::vector<AssetExplorerPanel::AssetEntry> &
    AssetExplorerPanel::filteredEntries(const std::vector<AssetEntry> &raw) {
        const bool dirty =
            cachedFilterGeneration != appliedRefreshGeneration ||
            cachedFilterSearch     != searchText              ||
            cachedFilterSortKey    != sortKey                  ||
            cachedFilterSortDir    != sortDir                  ||
            cachedFilterKind       != filterKind;

        if (!dirty) return cachedFilteredEntries;

        // ── rebuild ──────────────────────────────────────────────────────────
        cachedFilteredEntries = raw;  // copy once

        // Filter
        // OPT: use pre-lowercased nameLower — no per-entry string allocation
        const std::string query = lower(searchText);
        cachedFilteredEntries.erase(
            std::remove_if(cachedFilteredEntries.begin(), cachedFilteredEntries.end(),
                [&](const AssetEntry &e) {
                    if (!e.directory && !matchesFilter(e.kind, filterKind)) return true;
                    if (!query.empty() && e.nameLower.find(query) == std::string::npos) return true;
                    return false;
                }),
            cachedFilteredEntries.end());

        // Sort: folders first, then files by sort key
        // OPT: comparator uses nameLower — O(N log N) comparisons without allocations
        std::stable_sort(cachedFilteredEntries.begin(), cachedFilteredEntries.end(),
            [&](const AssetEntry &a, const AssetEntry &b) {
                if (a.directory != b.directory) return a.directory > b.directory;
                if (a.directory) return a.nameLower < b.nameLower;

                int r = 0;
                switch (sortKey) {
                    case SortKey::Name: r = a.nameLower.compare(b.nameLower); break;
                    case SortKey::Type: r = static_cast<int>(a.kind) - static_cast<int>(b.kind); break;
                    case SortKey::Size: r = (a.bytes < b.bytes) ? -1 : (a.bytes > b.bytes) ? 1 : 0; break;
                    case SortKey::Date: r = (a.lastWriteTime < b.lastWriteTime) ? -1 : 1; break;
                }
                return r * sortDir < 0;
            });

        // Update cache keys
        cachedFilterGeneration = appliedRefreshGeneration;
        cachedFilterSearch     = searchText;
        cachedFilterSortKey    = sortKey;
        cachedFilterSortDir    = sortDir;
        cachedFilterKind       = filterKind;

        return cachedFilteredEntries;
    }

    const AssetExplorerPanel::AssetEntry *AssetExplorerPanel::findSelected(
        const std::vector<AssetEntry> &entries) const {
        if (selectedPath.empty()) return nullptr;
        for (const auto &e: entries) {
            if (e.path == selectedPath) return &e;
        }
        return nullptr;
    }

    // ── classification ────────────────────────────────────────────────────────

    AssetExplorerPanel::AssetKind AssetExplorerPanel::classify(const std::filesystem::path &path, bool directory) {
        if (directory) return AssetKind::Folder;
        const std::string ext = lower(path.extension().string());
        if (ext == ".atlaslevel") return AssetKind::Level;
        if (ext == ".atlasmat" || ext == ".mat" || ext == ".mtl") return AssetKind::Material;
        if (ext == ".atlasmesh" || ext == ".gltf" || ext == ".glb" || ext == ".obj" || ext == ".fbx") return AssetKind::Model;
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga")
            return AssetKind::Texture;
        if (ext == ".hdr" || ext == ".ktx" || ext == ".ktx2") return AssetKind::Cubemap;
        if (ext == ".wav" || ext == ".mp3" || ext == ".ogg") return AssetKind::Audio;
        if (ext == ".vert" || ext == ".frag" || ext == ".comp" || ext == ".glsl" ||
            ext == ".billboard")
            return AssetKind::Shader;
        if (ext == ".cpp" || ext == ".hpp" || ext == ".h" || ext == ".cs" || ext == ".lua")
            return AssetKind::Script;
        if (ext == ".ttf") return AssetKind::Font;
        return AssetKind::Other;
    }

    bool AssetExplorerPanel::isTextureKind(AssetKind kind) {
        return kind == AssetKind::Texture || kind == AssetKind::Cubemap;
    }

    bool AssetExplorerPanel::matchesFilter(AssetKind kind, FilterKind filter) {
        switch (filter) {
            case FilterKind::All: return true;
            case FilterKind::Model: return kind == AssetKind::Model;
            case FilterKind::Material: return kind == AssetKind::Material;
            case FilterKind::Texture: return kind == AssetKind::Texture || kind == AssetKind::Cubemap;
            case FilterKind::Shader: return kind == AssetKind::Shader || kind == AssetKind::Script;
            case FilterKind::Font: return kind == AssetKind::Font;
            case FilterKind::Other: return kind == AssetKind::Audio || kind == AssetKind::Level ||
                                           kind == AssetKind::Other;
        }
        return true;
    }

    // ── kind metadata ─────────────────────────────────────────────────────────

    const char *AssetExplorerPanel::kindBadge(AssetKind kind) {
        switch (kind) {
            case AssetKind::Folder: return "DIR";
            case AssetKind::Level: return "SCN";
            case AssetKind::Material: return "MAT";
            case AssetKind::Model: return "MDL";
            case AssetKind::Texture: return "TEX";
            case AssetKind::Cubemap: return "CUBE";
            case AssetKind::Audio: return "AUD";
            case AssetKind::Shader: return "SHD";
            case AssetKind::Script: return "SRC";
            case AssetKind::Font: return "TTF";
            case AssetKind::Other: return "FILE";
        }
        return "FILE";
    }

    const char *AssetExplorerPanel::kindName(AssetKind kind) {
        switch (kind) {
            case AssetKind::Folder: return "Folder";
            case AssetKind::Level: return "Scene";
            case AssetKind::Material: return "Material";
            case AssetKind::Model: return "Model";
            case AssetKind::Texture: return "Texture";
            case AssetKind::Cubemap: return "Cubemap";
            case AssetKind::Audio: return "Audio";
            case AssetKind::Shader: return "Shader";
            case AssetKind::Script: return "Script";
            case AssetKind::Font: return "Font";
            case AssetKind::Other: return "File";
        }
        return "File";
    }

    ImU32 AssetExplorerPanel::kindColor(AssetKind kind) {
        switch (kind) {
            case AssetKind::Folder: return EditorTheme::colorU32(EditorTheme::Color::AssetFolder);
            case AssetKind::Level: return EditorTheme::colorU32(EditorTheme::Color::AssetLevel);
            case AssetKind::Material: return EditorTheme::colorU32(EditorTheme::Color::AssetMaterial);
            case AssetKind::Model: return EditorTheme::colorU32(EditorTheme::Color::AssetModel);
            case AssetKind::Texture: return EditorTheme::colorU32(EditorTheme::Color::AssetTexture);
            case AssetKind::Cubemap: return EditorTheme::colorU32(EditorTheme::Color::AssetCubemap);
            case AssetKind::Audio: return EditorTheme::colorU32(EditorTheme::Color::AssetAudio);
            case AssetKind::Shader: return EditorTheme::colorU32(EditorTheme::Color::AssetShader);
            case AssetKind::Script: return EditorTheme::colorU32(EditorTheme::Color::AssetScript);
            case AssetKind::Font: return EditorTheme::colorU32(EditorTheme::Color::AssetFont);
            case AssetKind::Other: return EditorTheme::colorU32(EditorTheme::Color::AssetOther);
        }
        return EditorTheme::colorU32(EditorTheme::Color::AssetOther);
    }

    // OPT: pre-computed table — replaces per-call float conversion round-trip.
    //      Built once on first use; EditorTheme colors are stable after init.
    ImU32 AssetExplorerPanel::kindColorBg(AssetKind kind) {
        static ImU32 cache[11] = {};
        static bool  built     = false;
        if (!built) {
            for (int i = 0; i <= static_cast<int>(AssetKind::Other); ++i) {
                ImVec4 v = ImGui::ColorConvertU32ToFloat4(kindColor(static_cast<AssetKind>(i)));
                v.w = 0.24f;
                cache[i] = ImGui::ColorConvertFloat4ToU32(v);
            }
            built = true;
        }
        const int idx = static_cast<int>(kind);
        return (idx >= 0 && idx <= static_cast<int>(AssetKind::Other)) ? cache[idx] : cache[static_cast<int>(AssetKind::Other)];
    }

    // ── formatting ────────────────────────────────────────────────────────────

    std::string AssetExplorerPanel::extensionBadge(const std::filesystem::path &path) {
        std::string ext = path.extension().string();
        if (!ext.empty() && ext.front() == '.') {
            ext.erase(ext.begin());
        }
        if (ext.empty()) {
            return {};
        }
        std::ranges::transform(ext, ext.begin(),
                               [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return ext;
    }

    std::string AssetExplorerPanel::formatSize(uintmax_t bytes) {
        if (bytes == 0 || bytes == static_cast<uintmax_t>(-1)) return "—";
        if (bytes < 1024) return std::to_string(bytes) + " B";
        if (bytes < 1024 * 1024)
            return std::to_string(bytes / 1024) + " KB";
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f MB",
                      static_cast<double>(bytes) / (1024.0 * 1024.0));
        return buf;
    }

    std::string AssetExplorerPanel::formatDate(const std::filesystem::file_time_type &ftime) {
        try {
            const auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
            std::time_t t = std::chrono::system_clock::to_time_t(sys);
            char buf[32];
            std::tm tm{};
            localtime_s(&tm, &t);
            std::strftime(buf, sizeof(buf), "%b %d, %Y", &tm);
            return buf;
        } catch (...) {
            return "—";
        }
    }

    std::string AssetExplorerPanel::lower(std::string value) {
        std::ranges::transform(value, value.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    std::string AssetExplorerPanel::displayPath(const std::filesystem::path &path) {
        return path.generic_string();
    }

    std::filesystem::path AssetExplorerPanel::relativeTo(const std::filesystem::path &path,
                                                         const std::filesystem::path &root) {
        std::error_code err;
        auto rel = std::filesystem::relative(path, root, err);
        return err ? path.filename() : rel;
    }
} // namespace Atlas::Editor