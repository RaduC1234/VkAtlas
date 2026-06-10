#pragma once

#include <imgui.h>

namespace Atlas::Editor {
    class AssetExplorerComponents {
    public:
        enum class Color {
            SelectionRing,
            SelectionSoft,
            SelectionBar,
            Toolbar,
            Strip,
            TreeBg,
            TileBg,
            TileBorder,
            TileHover,
            GridLine,
            Separator,
            Text,
            TextDim,
            BadgeText,
            DetailRow
        };

        static ImU32 color(Color color, float alphaScale = 1.0f);
        static ImVec4 colorVec4(Color color, float alphaScale = 1.0f);
        static ImU32 withAlpha(ImU32 color, float alphaScale);

        static float toolbarHeight();
        static float stripHeight();
        static float treeWidth();
        static float tileMin();
        static float tileGap();
        static float tileLabelHeight();
        static float tileRounding();
        static float rowRounding();

        static bool toolbarButton(const char *label, const char *tooltip, bool active, ImVec2 size);
        static bool toolbarIconButton(const char *id, ImTextureID tex, ImVec2 iconSize, const char *tooltip, bool active, ImVec2 btnSize);
        static bool breadcrumbButton(const char *label, bool current);

        static void separatorLine(ImDrawList *drawList, ImVec2 start, ImVec2 end);
        static void tileBackground(ImDrawList *drawList, ImVec2 min, ImVec2 max, ImU32 tint, bool selected, bool hovered);
        static void thumbnailGrid(ImDrawList *drawList, ImVec2 min, ImVec2 max);
        static void folderIcon(ImDrawList *drawList, ImVec2 min, ImVec2 max, ImU32 color);
        static void cubeIcon(ImDrawList *drawList, ImVec2 min, ImVec2 max, ImU32 color);
        static void materialIcon(ImDrawList *drawList, ImVec2 min, ImVec2 max, ImU32 color);
        static void fileIcon(ImDrawList *drawList, ImVec2 min, ImVec2 max, ImU32 color);
        static void badge(ImDrawList *drawList, ImVec2 min, const char *text, ImU32 fill);
        static void textEllipsis(ImDrawList *drawList, ImVec2 pos, float maxWidth, const char *text, ImU32 color);
    };
}
