#pragma once

#include <imgui.h>

namespace Atlas::Editor {
    class EditorTheme {
    public:
        enum class Mode {
            Dark,
            Light
        };

        enum class Color {
            Text,
            TextMuted,
            WindowBg,
            PanelBg,
            PopupBg,
            Surface,
            SurfaceHover,
            SurfaceActive,
            Border,
            BorderStrong,
            Accent,
            AccentHover,
            AccentMuted,
            Selection,
            OverlayBg,
            OverlayBorder,
            OverlayShadow,
            OverlayHover,
            AssetExplorerToolbar,
            AssetExplorerStrip,
            AssetExplorerTreeBg,
            AssetExplorerTileBg,
            AssetExplorerTileBorder,
            AssetExplorerTileHover,
            AssetExplorerGridLine,
            AssetExplorerSeparator,
            AssetExplorerSelectionRing,
            AssetExplorerSelectionSoft,
            AssetExplorerSelectionBar,
            AssetExplorerBadgeText,
            AssetExplorerDetailRow,
            AssetFolder,
            AssetLevel,
            AssetMaterial,
            AssetModel,
            AssetTexture,
            AssetCubemap,
            AssetAudio,
            AssetShader,
            AssetScript,
            AssetFont,
            AssetOther,
            AxisX,
            AxisY,
            AxisZ,
            White,
            Black
        };

        enum class Metric {
            WindowPaddingX,
            WindowPaddingY,
            FramePaddingX,
            FramePaddingY,
            ItemSpacingX,
            ItemSpacingY,
            ItemInnerSpacingX,
            ItemInnerSpacingY,
            IndentSpacing,
            ScrollbarSize,
            GrabMinSize,
            WindowRounding,
            ChildRounding,
            PopupRounding,
            FrameRounding,
            TabRounding,
            ToolbarIconSize,
            ToolbarButtonWidth,
            ToolbarButtonHeight,
            ToolbarIslandPadding,
            ToolbarIslandRounding,
            ToolbarIslandMargin,
            AssetExplorerToolbarHeight,
            AssetExplorerStripHeight,
            AssetExplorerTreeWidth,
            AssetExplorerTileMin,
            AssetExplorerTileGap,
            AssetExplorerTileLabelHeight,
            AssetExplorerTileRounding,
            AssetExplorerRowRounding
        };

        static void apply(Mode mode);
        static Mode mode();

        static ImVec4 color(Color color);
        static ImU32 colorU32(Color color, float alphaScale = 1.0f);
        static float metric(Metric metric);

    private:
        static Mode activeMode;

        static void applySharedMetrics();
        static void applyDarkColors();
        static void applyLightColors();
        static ImVec4 srgb(ImVec4 color);
        static ImVec4 rgba(int r, int g, int b, int a = 255);
    };

    class ScopedColor {
    public:
        ScopedColor(ImGuiCol target, EditorTheme::Color color);
        ScopedColor(ImGuiCol target, ImVec4 color);
        ~ScopedColor();

        ScopedColor(const ScopedColor &) = delete;
        ScopedColor &operator=(const ScopedColor &) = delete;

    private:
        bool active = false;
    };

    class ScopedStyleVar {
    public:
        ScopedStyleVar(ImGuiStyleVar target, float value);
        ScopedStyleVar(ImGuiStyleVar target, ImVec2 value);
        ~ScopedStyleVar();

        ScopedStyleVar(const ScopedStyleVar &) = delete;
        ScopedStyleVar &operator=(const ScopedStyleVar &) = delete;

    private:
        bool active = false;
    };
}
