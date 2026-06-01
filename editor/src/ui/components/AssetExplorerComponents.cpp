#include "AssetExplorerComponents.hpp"

#include "ui/theme/EditorTheme.hpp"

#include <algorithm>
#include <cstring>
#include <string>

namespace Atlas::Editor {
    ImU32 AssetExplorerComponents::color(const Color color, const float alphaScale) {
        switch (color) {
            case Color::SelectionRing: return EditorTheme::colorU32(EditorTheme::Color::AssetExplorerSelectionRing, alphaScale);
            case Color::SelectionSoft: return EditorTheme::colorU32(EditorTheme::Color::AssetExplorerSelectionSoft, alphaScale);
            case Color::SelectionBar: return EditorTheme::colorU32(EditorTheme::Color::AssetExplorerSelectionBar, alphaScale);
            case Color::Toolbar: return EditorTheme::colorU32(EditorTheme::Color::AssetExplorerToolbar, alphaScale);
            case Color::Strip: return EditorTheme::colorU32(EditorTheme::Color::AssetExplorerStrip, alphaScale);
            case Color::TreeBg: return EditorTheme::colorU32(EditorTheme::Color::AssetExplorerTreeBg, alphaScale);
            case Color::TileBg: return EditorTheme::colorU32(EditorTheme::Color::AssetExplorerTileBg, alphaScale);
            case Color::TileBorder: return EditorTheme::colorU32(EditorTheme::Color::AssetExplorerTileBorder, alphaScale);
            case Color::TileHover: return EditorTheme::colorU32(EditorTheme::Color::AssetExplorerTileHover, alphaScale);
            case Color::GridLine: return EditorTheme::colorU32(EditorTheme::Color::AssetExplorerGridLine, alphaScale);
            case Color::Separator: return EditorTheme::colorU32(EditorTheme::Color::AssetExplorerSeparator, alphaScale);
            case Color::Text: return EditorTheme::colorU32(EditorTheme::Color::Text, alphaScale);
            case Color::TextDim: return EditorTheme::colorU32(EditorTheme::Color::TextMuted, alphaScale);
            case Color::BadgeText: return EditorTheme::colorU32(EditorTheme::Color::AssetExplorerBadgeText, alphaScale);
            case Color::DetailRow: return EditorTheme::colorU32(EditorTheme::Color::AssetExplorerDetailRow, alphaScale);
        }
        return EditorTheme::colorU32(EditorTheme::Color::Text, alphaScale);
    }

    ImVec4 AssetExplorerComponents::colorVec4(const Color color, const float alphaScale) {
        return ImGui::ColorConvertU32ToFloat4(AssetExplorerComponents::color(color, alphaScale));
    }

    ImU32 AssetExplorerComponents::withAlpha(const ImU32 color, const float alphaScale) {
        ImVec4 value = ImGui::ColorConvertU32ToFloat4(color);
        value.w *= alphaScale;
        return ImGui::ColorConvertFloat4ToU32(value);
    }

    float AssetExplorerComponents::toolbarHeight() {
        return EditorTheme::metric(EditorTheme::Metric::AssetExplorerToolbarHeight);
    }

    float AssetExplorerComponents::stripHeight() {
        return EditorTheme::metric(EditorTheme::Metric::AssetExplorerStripHeight);
    }

    float AssetExplorerComponents::treeWidth() {
        return EditorTheme::metric(EditorTheme::Metric::AssetExplorerTreeWidth);
    }

    float AssetExplorerComponents::tileMin() {
        return EditorTheme::metric(EditorTheme::Metric::AssetExplorerTileMin);
    }

    float AssetExplorerComponents::tileGap() {
        return EditorTheme::metric(EditorTheme::Metric::AssetExplorerTileGap);
    }

    float AssetExplorerComponents::tileLabelHeight() {
        return EditorTheme::metric(EditorTheme::Metric::AssetExplorerTileLabelHeight);
    }

    float AssetExplorerComponents::tileRounding() {
        return EditorTheme::metric(EditorTheme::Metric::AssetExplorerTileRounding);
    }

    float AssetExplorerComponents::rowRounding() {
        return EditorTheme::metric(EditorTheme::Metric::AssetExplorerRowRounding);
    }

    bool AssetExplorerComponents::toolbarButton(const char *label, const char *tooltip, const bool active, const ImVec2 size) {
        int pushed = 0;
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, EditorTheme::color(EditorTheme::Color::Accent));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EditorTheme::color(EditorTheme::Color::AccentHover));
            ImGui::PushStyleColor(ImGuiCol_Text, EditorTheme::color(EditorTheme::Color::AssetExplorerBadgeText));
            pushed = 3;
        }

        const bool clicked = ImGui::Button(label, size);
        if (pushed > 0) {
            ImGui::PopStyleColor(pushed);
        }

        if (tooltip != nullptr && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tooltip);
        }
        return clicked;
    }

    bool AssetExplorerComponents::breadcrumbButton(const char *label, const bool current) {
        int pushed = 0;
        if (current) {
            ImGui::PushStyleColor(ImGuiCol_Button, EditorTheme::color(EditorTheme::Color::SurfaceHover));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EditorTheme::color(EditorTheme::Color::SurfaceActive));
            ImGui::PushStyleColor(ImGuiCol_Text, EditorTheme::color(EditorTheme::Color::Text));
            pushed = 3;
        }

        const bool clicked = ImGui::SmallButton(label);
        if (pushed > 0) {
            ImGui::PopStyleColor(pushed);
        }
        return clicked;
    }

    void AssetExplorerComponents::separatorLine(ImDrawList *drawList, const ImVec2 start, const ImVec2 end) {
        drawList->AddLine(start, end, color(Color::Separator), 1.0f);
    }

    void AssetExplorerComponents::tileBackground(
        ImDrawList *drawList,
        const ImVec2 min,
        const ImVec2 max,
        const ImU32 tint,
        const bool selected,
        const bool hovered) {
        const float rounding = tileRounding();
        drawList->AddRectFilled(min, max, hovered ? color(Color::TileHover) : color(Color::TileBg), rounding);

        drawList->AddRectFilled(min, max, withAlpha(tint, 0.32f), rounding);

        const ImU32 transparent = color(Color::TileBg, 0.0f);
        const ImVec2 fadeMin(min.x + 1.0f, min.y + rounding);
        const ImVec2 fadeMax(max.x - 1.0f, max.y - 1.0f);
        drawList->AddRectFilledMultiColor(
            fadeMin,
            fadeMax,
            withAlpha(tint, 0.22f),
            withAlpha(tint, 0.22f),
            transparent,
            transparent);
        drawList->AddRect(
            ImVec2(min.x + 1.0f, min.y + 1.0f),
            ImVec2(max.x - 1.0f, max.y - 1.0f),
            color(Color::GridLine, 0.34f),
            rounding - 1.0f,
            0,
            1.0f);

        if (selected) {
            drawList->AddRect(min, max, color(Color::SelectionRing), rounding, 0, 1.5f);
        } else {
            drawList->AddRect(min, max, color(Color::TileBorder, 0.72f), rounding, 0, 1.0f);
        }
    }

    void AssetExplorerComponents::thumbnailGrid(ImDrawList *drawList, const ImVec2 min, const ImVec2 max) {
        constexpr float gridStep = 20.0f;
        const ImU32 grid = color(Color::GridLine, 0.44f);
        for (float x = min.x + gridStep; x < max.x; x += gridStep) {
            drawList->AddLine(ImVec2(x, min.y), ImVec2(x, max.y), grid, 1.0f);
        }
        for (float y = min.y + gridStep; y < max.y; y += gridStep) {
            drawList->AddLine(ImVec2(min.x, y), ImVec2(max.x, y), grid, 1.0f);
        }
    }

    void AssetExplorerComponents::folderIcon(ImDrawList *drawList, const ImVec2 min, const ImVec2 max, const ImU32 color) {
        const float width = max.x - min.x;
        const float height = max.y - min.y;
        const float folderW = width * 0.54f;
        const float folderH = folderW * 0.78f;
        const ImVec2 folderMin(min.x + (width - folderW) * 0.5f, min.y + (height - folderH) * 0.48f);
        const ImVec2 folderMax(folderMin.x + folderW, folderMin.y + folderH);
        const ImVec2 tabMax(folderMin.x + folderW * 0.44f, folderMin.y + folderH * 0.22f);

        drawList->AddRectFilled(
            ImVec2(folderMin.x, folderMin.y + folderH * 0.20f),
            folderMax,
            color,
            4.0f,
            ImDrawFlags_RoundCornersAll);
        drawList->AddRectFilled(folderMin, tabMax, color, 3.0f);
    }

    void AssetExplorerComponents::cubeIcon(ImDrawList *drawList, const ImVec2 min, const ImVec2 max, const ImU32 color) {
        const float width = max.x - min.x;
        const float height = max.y - min.y;
        const float side = std::min(width, height) * 0.34f;
        const float depth = side * 0.38f;
        const ImVec2 center(min.x + width * 0.50f, min.y + height * 0.52f);

        const ImVec2 front0(center.x - side * 0.58f, center.y - side * 0.42f);
        const ImVec2 front1(center.x + side * 0.42f, center.y - side * 0.42f);
        const ImVec2 front2(center.x + side * 0.42f, center.y + side * 0.58f);
        const ImVec2 front3(center.x - side * 0.58f, center.y + side * 0.58f);

        const ImVec2 back0(front0.x + depth, front0.y - depth);
        const ImVec2 back1(front1.x + depth, front1.y - depth);
        const ImVec2 back2(front2.x + depth, front2.y - depth);
        const ImVec2 back3(front3.x + depth, front3.y - depth);

        drawList->AddQuadFilled(front0, front1, front2, front3, withAlpha(color, 0.16f));
        drawList->AddQuadFilled(back0, back1, front1, front0, withAlpha(color, 0.28f));
        drawList->AddQuadFilled(front1, back1, back2, front2, withAlpha(color, 0.22f));

        const ImU32 backLine = withAlpha(color, 0.45f);
        drawList->AddLine(back0, back1, backLine, 1.2f);
        drawList->AddLine(back1, back2, backLine, 1.2f);
        drawList->AddLine(back2, back3, backLine, 1.2f);
        drawList->AddLine(back3, back0, backLine, 1.2f);

        const ImU32 line = withAlpha(color, 0.88f);
        const float thickness = std::max(1.2f, side * 0.045f);
        drawList->AddLine(front0, front1, line, thickness);
        drawList->AddLine(front1, front2, line, thickness);
        drawList->AddLine(front2, front3, line, thickness);
        drawList->AddLine(front3, front0, line, thickness);
        drawList->AddLine(front0, back0, line, thickness);
        drawList->AddLine(front1, back1, line, thickness);
        drawList->AddLine(front2, back2, line, thickness);
    }

    void AssetExplorerComponents::materialIcon(ImDrawList *drawList, const ImVec2 min, const ImVec2 max, const ImU32 color) {
        const float width = max.x - min.x;
        const float height = max.y - min.y;
        const float radius = std::min(width, height) * 0.20f;
        const ImVec2 center(min.x + width * 0.50f, min.y + height * 0.52f);

        drawList->AddCircleFilled(center, radius, withAlpha(color, 0.18f), 32);
        drawList->AddCircle(center, radius, withAlpha(color, 0.92f), 32, std::max(1.2f, radius * 0.11f));
        drawList->AddCircleFilled(
            ImVec2(center.x - radius * 0.34f, center.y - radius * 0.38f),
            std::max(1.6f, radius * 0.22f),
            withAlpha(AssetExplorerComponents::color(Color::BadgeText), 0.32f),
            16);

        const ImU32 arc = withAlpha(color, 0.62f);
        drawList->PathArcTo(center, radius * 0.66f, 0.20f, 2.38f, 18);
        drawList->PathStroke(arc, 0, std::max(1.0f, radius * 0.08f));
    }

    void AssetExplorerComponents::fileIcon(ImDrawList *drawList, const ImVec2 min, const ImVec2 max, const ImU32 color) {
        const float width = max.x - min.x;
        const float height = max.y - min.y;
        const float iconW = width * 0.34f;
        const float iconH = height * 0.46f;
        const ImVec2 iconMin(min.x + (width - iconW) * 0.5f, min.y + (height - iconH) * 0.50f);
        const ImVec2 iconMax(iconMin.x + iconW, iconMin.y + iconH);
        const float fold = iconW * 0.26f;

        drawList->AddRectFilled(iconMin, iconMax, withAlpha(color, 0.15f), 3.0f);
        drawList->AddTriangleFilled(
            ImVec2(iconMax.x - fold, iconMin.y),
            ImVec2(iconMax.x, iconMin.y + fold),
            ImVec2(iconMax.x - fold, iconMin.y + fold),
            withAlpha(color, 0.32f));
        drawList->AddRect(iconMin, iconMax, withAlpha(color, 0.76f), 3.0f, 0, 1.2f);
        drawList->AddLine(ImVec2(iconMin.x + iconW * 0.22f, iconMin.y + iconH * 0.58f),
                          ImVec2(iconMax.x - iconW * 0.22f, iconMin.y + iconH * 0.58f),
                          withAlpha(color, 0.55f), 1.0f);
        drawList->AddLine(ImVec2(iconMin.x + iconW * 0.22f, iconMin.y + iconH * 0.74f),
                          ImVec2(iconMax.x - iconW * 0.32f, iconMin.y + iconH * 0.74f),
                          withAlpha(color, 0.42f), 1.0f);
    }

    void AssetExplorerComponents::badge(ImDrawList *drawList, const ImVec2 min, const char *text, const ImU32 fill) {
        const ImVec2 textSize = ImGui::CalcTextSize(text);
        const float width = std::max(36.0f, textSize.x + 12.0f);
        constexpr float height = 18.0f;
        constexpr float rounding = 5.0f;
        const ImVec2 max(min.x + width, min.y + height);

        drawList->PathRect(min, max, rounding, ImDrawFlags_RoundCornersAll);
        drawList->PathFillConvex(fill);
        drawList->AddText(ImVec2(min.x + 6.0f, min.y + 2.0f), color(Color::BadgeText), text);
    }

    void AssetExplorerComponents::textEllipsis(
        ImDrawList *drawList,
        const ImVec2 pos,
        const float maxWidth,
        const char *text,
        const ImU32 color) {
        const float textWidth = ImGui::CalcTextSize(text).x;
        if (textWidth <= maxWidth) {
            drawList->AddText(pos, color, text);
            return;
        }

        constexpr const char *ellipsis = "...";
        const float ellipsisWidth = ImGui::CalcTextSize(ellipsis).x;
        const float available = maxWidth - ellipsisWidth;
        if (available <= 0.0f) {
            drawList->AddText(pos, color, ellipsis);
            return;
        }

        const char *fit = text + std::strlen(text);
        while (fit > text && ImGui::CalcTextSize(text, fit).x > available) {
            --fit;
        }

        std::string truncated(text, fit);
        truncated += ellipsis;
        drawList->AddText(pos, color, truncated.c_str());
    }
}
