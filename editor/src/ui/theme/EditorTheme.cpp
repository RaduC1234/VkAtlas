#include "EditorTheme.hpp"

#include <cmath>

namespace Atlas::Editor {
    EditorTheme::Mode EditorTheme::activeMode = EditorTheme::Mode::Dark;

    void EditorTheme::apply(const Mode mode) {
        activeMode = mode;
        applySharedMetrics();

        switch (mode) {
            case Mode::Dark:
                applyDarkColors();
                break;
            case Mode::Light:
                applyLightColors();
                break;
        }
    }

    EditorTheme::Mode EditorTheme::mode() {
        return activeMode;
    }

    void EditorTheme::applySharedMetrics() {
        ImGuiStyle &style = ImGui::GetStyle();

        style.WindowRounding = metric(Metric::WindowRounding);
        style.ChildRounding = metric(Metric::ChildRounding);
        style.PopupRounding = metric(Metric::PopupRounding);
        style.FrameRounding = metric(Metric::FrameRounding);
        style.GrabRounding = metric(Metric::FrameRounding);
        style.TabRounding = metric(Metric::TabRounding);
        style.ScrollbarRounding = metric(Metric::FrameRounding);

        style.WindowBorderSize = 0.5f;
        style.FrameBorderSize = 0.5f;
        style.TabBorderSize = 0.0f;
        style.PopupBorderSize = 0.5f;

        style.WindowPadding = ImVec2(metric(Metric::WindowPaddingX), metric(Metric::WindowPaddingY));
        style.FramePadding = ImVec2(metric(Metric::FramePaddingX), metric(Metric::FramePaddingY));
        style.ItemSpacing = ImVec2(metric(Metric::ItemSpacingX), metric(Metric::ItemSpacingY));
        style.ItemInnerSpacing = ImVec2(metric(Metric::ItemInnerSpacingX), metric(Metric::ItemInnerSpacingY));
        style.IndentSpacing = metric(Metric::IndentSpacing);
        style.ScrollbarSize = metric(Metric::ScrollbarSize);
        style.GrabMinSize = metric(Metric::GrabMinSize);
    }

    void EditorTheme::applyDarkColors() {
        ImVec4 *c = ImGui::GetStyle().Colors;

        const ImVec4 accent = srgb(ImVec4(0.260f, 0.580f, 0.920f, 1.00f));
        const ImVec4 accentHover = srgb(ImVec4(0.360f, 0.660f, 1.000f, 1.00f));
        const ImVec4 accentDim = srgb(ImVec4(0.260f, 0.580f, 0.920f, 0.30f));

        c[ImGuiCol_Text] = srgb(ImVec4(0.880f, 0.880f, 0.880f, 1.00f));
        c[ImGuiCol_TextDisabled] = srgb(ImVec4(0.400f, 0.400f, 0.400f, 1.00f));

        c[ImGuiCol_WindowBg] = srgb(ImVec4(0.110f, 0.110f, 0.115f, 1.00f));
        c[ImGuiCol_ChildBg] = srgb(ImVec4(0.110f, 0.110f, 0.115f, 1.00f));
        c[ImGuiCol_PopupBg] = srgb(ImVec4(0.080f, 0.080f, 0.085f, 1.00f));

        c[ImGuiCol_Border] = srgb(ImVec4(1.000f, 1.000f, 1.000f, 0.07f));
        c[ImGuiCol_BorderShadow] = srgb(ImVec4(0.000f, 0.000f, 0.000f, 0.00f));

        c[ImGuiCol_FrameBg] = srgb(ImVec4(0.080f, 0.080f, 0.085f, 1.00f));
        c[ImGuiCol_FrameBgHovered] = srgb(ImVec4(0.140f, 0.140f, 0.148f, 1.00f));
        c[ImGuiCol_FrameBgActive] = srgb(ImVec4(0.160f, 0.200f, 0.260f, 1.00f));

        c[ImGuiCol_TitleBg] = srgb(ImVec4(0.080f, 0.080f, 0.085f, 1.00f));
        c[ImGuiCol_TitleBgActive] = srgb(ImVec4(0.080f, 0.080f, 0.085f, 1.00f));
        c[ImGuiCol_TitleBgCollapsed] = srgb(ImVec4(0.080f, 0.080f, 0.085f, 1.00f));
        c[ImGuiCol_MenuBarBg] = srgb(ImVec4(0.090f, 0.090f, 0.095f, 1.00f));

        c[ImGuiCol_ScrollbarBg] = srgb(ImVec4(0.090f, 0.090f, 0.095f, 1.00f));
        c[ImGuiCol_ScrollbarGrab] = srgb(ImVec4(0.280f, 0.280f, 0.290f, 1.00f));
        c[ImGuiCol_ScrollbarGrabHovered] = srgb(ImVec4(0.360f, 0.360f, 0.370f, 1.00f));
        c[ImGuiCol_ScrollbarGrabActive] = srgb(ImVec4(0.440f, 0.444f, 0.460f, 1.00f));

        c[ImGuiCol_CheckMark] = accent;
        c[ImGuiCol_SliderGrab] = accent;
        c[ImGuiCol_SliderGrabActive] = accentHover;

        c[ImGuiCol_Button] = srgb(ImVec4(0.180f, 0.180f, 0.190f, 1.00f));
        c[ImGuiCol_ButtonHovered] = srgb(ImVec4(0.220f, 0.225f, 0.235f, 1.00f));
        c[ImGuiCol_ButtonActive] = srgb(ImVec4(0.160f, 0.220f, 0.310f, 1.00f));

        c[ImGuiCol_Header] = srgb(ImVec4(0.180f, 0.180f, 0.190f, 1.00f));
        c[ImGuiCol_HeaderHovered] = srgb(ImVec4(0.220f, 0.225f, 0.235f, 1.00f));
        c[ImGuiCol_HeaderActive] = srgb(ImVec4(0.160f, 0.220f, 0.310f, 1.00f));

        c[ImGuiCol_Separator] = srgb(ImVec4(1.000f, 1.000f, 1.000f, 0.07f));
        c[ImGuiCol_SeparatorHovered] = accent;
        c[ImGuiCol_SeparatorActive] = accent;

        c[ImGuiCol_ResizeGrip] = accentDim;
        c[ImGuiCol_ResizeGripHovered] = accent;
        c[ImGuiCol_ResizeGripActive] = accentHover;

        c[ImGuiCol_Tab] = srgb(ImVec4(0.090f, 0.090f, 0.095f, 1.00f));
        c[ImGuiCol_TabHovered] = srgb(ImVec4(0.160f, 0.162f, 0.170f, 1.00f));
        c[ImGuiCol_TabActive] = srgb(ImVec4(0.110f, 0.110f, 0.115f, 1.00f));
        c[ImGuiCol_TabUnfocused] = srgb(ImVec4(0.090f, 0.090f, 0.095f, 1.00f));
        c[ImGuiCol_TabUnfocusedActive] = srgb(ImVec4(0.100f, 0.100f, 0.105f, 1.00f));

        c[ImGuiCol_DockingPreview] = srgb(ImVec4(0.260f, 0.580f, 0.920f, 0.25f));
        c[ImGuiCol_DockingEmptyBg] = srgb(ImVec4(0.080f, 0.080f, 0.085f, 1.00f));

        c[ImGuiCol_PlotLines] = accent;
        c[ImGuiCol_PlotLinesHovered] = accentHover;
        c[ImGuiCol_PlotHistogram] = accent;
        c[ImGuiCol_PlotHistogramHovered] = accentHover;

        c[ImGuiCol_TableHeaderBg] = srgb(ImVec4(0.130f, 0.130f, 0.138f, 1.00f));
        c[ImGuiCol_TableBorderStrong] = srgb(ImVec4(1.000f, 1.000f, 1.000f, 0.10f));
        c[ImGuiCol_TableBorderLight] = srgb(ImVec4(1.000f, 1.000f, 1.000f, 0.05f));
        c[ImGuiCol_TableRowBg] = srgb(ImVec4(0.000f, 0.000f, 0.000f, 0.00f));
        c[ImGuiCol_TableRowBgAlt] = srgb(ImVec4(1.000f, 1.000f, 1.000f, 0.02f));

        c[ImGuiCol_TextSelectedBg] = srgb(ImVec4(0.260f, 0.580f, 0.920f, 0.30f));
        c[ImGuiCol_DragDropTarget] = accent;
        c[ImGuiCol_NavHighlight] = accent;
        c[ImGuiCol_ModalWindowDimBg] = srgb(ImVec4(0.000f, 0.000f, 0.000f, 0.50f));
    }

    void EditorTheme::applyLightColors() {
        ImVec4 *c = ImGui::GetStyle().Colors;

        const ImVec4 accent = srgb(ImVec4(0.220f, 0.530f, 0.860f, 1.00f));
        const ImVec4 accentHover = srgb(ImVec4(0.280f, 0.590f, 0.920f, 1.00f));
        const ImVec4 accentDim = srgb(ImVec4(0.220f, 0.530f, 0.860f, 0.35f));

        c[ImGuiCol_Text] = srgb(ImVec4(0.100f, 0.100f, 0.100f, 1.00f));
        c[ImGuiCol_TextDisabled] = srgb(ImVec4(0.580f, 0.580f, 0.580f, 1.00f));

        c[ImGuiCol_WindowBg] = srgb(ImVec4(0.960f, 0.960f, 0.960f, 1.00f));
        c[ImGuiCol_ChildBg] = srgb(ImVec4(0.960f, 0.960f, 0.960f, 1.00f));
        c[ImGuiCol_PopupBg] = srgb(ImVec4(0.980f, 0.980f, 0.980f, 1.00f));

        c[ImGuiCol_Border] = srgb(ImVec4(0.800f, 0.800f, 0.800f, 0.60f));
        c[ImGuiCol_BorderShadow] = srgb(ImVec4(0.000f, 0.000f, 0.000f, 0.00f));

        c[ImGuiCol_FrameBg] = srgb(ImVec4(0.920f, 0.920f, 0.920f, 1.00f));
        c[ImGuiCol_FrameBgHovered] = srgb(ImVec4(0.880f, 0.888f, 0.900f, 1.00f));
        c[ImGuiCol_FrameBgActive] = srgb(ImVec4(0.840f, 0.870f, 0.920f, 1.00f));

        c[ImGuiCol_TitleBg] = srgb(ImVec4(0.930f, 0.930f, 0.930f, 1.00f));
        c[ImGuiCol_TitleBgActive] = srgb(ImVec4(0.930f, 0.930f, 0.930f, 1.00f));
        c[ImGuiCol_TitleBgCollapsed] = srgb(ImVec4(0.930f, 0.930f, 0.930f, 1.00f));
        c[ImGuiCol_MenuBarBg] = srgb(ImVec4(0.980f, 0.980f, 0.980f, 1.00f));

        c[ImGuiCol_ScrollbarBg] = srgb(ImVec4(0.940f, 0.940f, 0.940f, 1.00f));
        c[ImGuiCol_ScrollbarGrab] = srgb(ImVec4(0.720f, 0.720f, 0.720f, 1.00f));
        c[ImGuiCol_ScrollbarGrabHovered] = srgb(ImVec4(0.620f, 0.620f, 0.620f, 1.00f));
        c[ImGuiCol_ScrollbarGrabActive] = srgb(ImVec4(0.500f, 0.500f, 0.500f, 1.00f));

        c[ImGuiCol_CheckMark] = accent;
        c[ImGuiCol_SliderGrab] = accent;
        c[ImGuiCol_SliderGrabActive] = accentHover;

        c[ImGuiCol_Button] = srgb(ImVec4(0.900f, 0.900f, 0.900f, 1.00f));
        c[ImGuiCol_ButtonHovered] = srgb(ImVec4(0.860f, 0.870f, 0.890f, 1.00f));
        c[ImGuiCol_ButtonActive] = srgb(ImVec4(0.820f, 0.850f, 0.920f, 1.00f));

        c[ImGuiCol_Header] = srgb(ImVec4(0.900f, 0.900f, 0.900f, 1.00f));
        c[ImGuiCol_HeaderHovered] = srgb(ImVec4(0.870f, 0.878f, 0.895f, 1.00f));
        c[ImGuiCol_HeaderActive] = srgb(ImVec4(0.820f, 0.850f, 0.920f, 1.00f));

        c[ImGuiCol_Separator] = srgb(ImVec4(0.800f, 0.800f, 0.800f, 0.60f));
        c[ImGuiCol_SeparatorHovered] = accent;
        c[ImGuiCol_SeparatorActive] = accent;

        c[ImGuiCol_ResizeGrip] = accentDim;
        c[ImGuiCol_ResizeGripHovered] = accent;
        c[ImGuiCol_ResizeGripActive] = accentHover;

        c[ImGuiCol_Tab] = srgb(ImVec4(0.940f, 0.940f, 0.940f, 1.00f));
        c[ImGuiCol_TabHovered] = srgb(ImVec4(0.900f, 0.905f, 0.915f, 1.00f));
        c[ImGuiCol_TabActive] = srgb(ImVec4(0.960f, 0.960f, 0.960f, 1.00f));
        c[ImGuiCol_TabUnfocused] = srgb(ImVec4(0.930f, 0.930f, 0.930f, 1.00f));
        c[ImGuiCol_TabUnfocusedActive] = srgb(ImVec4(0.950f, 0.950f, 0.950f, 1.00f));

        c[ImGuiCol_DockingPreview] = srgb(ImVec4(0.220f, 0.530f, 0.860f, 0.30f));
        c[ImGuiCol_DockingEmptyBg] = srgb(ImVec4(0.920f, 0.920f, 0.920f, 1.00f));

        c[ImGuiCol_PlotLines] = accent;
        c[ImGuiCol_PlotLinesHovered] = accentHover;
        c[ImGuiCol_PlotHistogram] = accent;
        c[ImGuiCol_PlotHistogramHovered] = accentHover;

        c[ImGuiCol_TableHeaderBg] = srgb(ImVec4(0.920f, 0.922f, 0.928f, 1.00f));
        c[ImGuiCol_TableBorderStrong] = srgb(ImVec4(0.780f, 0.780f, 0.780f, 1.00f));
        c[ImGuiCol_TableBorderLight] = srgb(ImVec4(0.860f, 0.860f, 0.860f, 1.00f));
        c[ImGuiCol_TableRowBg] = srgb(ImVec4(0.000f, 0.000f, 0.000f, 0.00f));
        c[ImGuiCol_TableRowBgAlt] = srgb(ImVec4(0.000f, 0.000f, 0.000f, 0.02f));

        c[ImGuiCol_TextSelectedBg] = srgb(ImVec4(0.220f, 0.530f, 0.860f, 0.30f));
        c[ImGuiCol_DragDropTarget] = accent;
        c[ImGuiCol_NavHighlight] = accent;
        c[ImGuiCol_ModalWindowDimBg] = srgb(ImVec4(0.100f, 0.100f, 0.100f, 0.40f));
    }

    ImVec4 EditorTheme::color(const Color color) {
        switch (color) {
            case Color::Text: return ImGui::GetStyleColorVec4(ImGuiCol_Text);
            case Color::TextMuted: return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
            case Color::WindowBg: return ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
            case Color::PanelBg: return ImGui::GetStyleColorVec4(ImGuiCol_ChildBg);
            case Color::PopupBg: return ImGui::GetStyleColorVec4(ImGuiCol_PopupBg);
            case Color::Surface: return ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
            case Color::SurfaceHover: return ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered);
            case Color::SurfaceActive: return ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive);
            case Color::Border: return ImGui::GetStyleColorVec4(ImGuiCol_Border);
            case Color::BorderStrong: return mode() == Mode::Dark ? rgba(255, 255, 255, 26) : rgba(120, 120, 120, 255);
            case Color::Accent: return mode() == Mode::Dark ? rgba(88, 140, 230) : rgba(56, 135, 219);
            case Color::AccentHover: return mode() == Mode::Dark ? rgba(92, 168, 255) : rgba(71, 150, 235);
            case Color::AccentMuted: return mode() == Mode::Dark ? rgba(88, 140, 230, 76) : rgba(56, 135, 219, 89);
            case Color::Selection: return ImGui::GetStyleColorVec4(ImGuiCol_TextSelectedBg);
            case Color::OverlayBg: return mode() == Mode::Dark ? rgba(24, 24, 27, 218) : rgba(242, 242, 244, 230);
            case Color::OverlayBorder: return mode() == Mode::Dark ? rgba(255, 255, 255, 34) : rgba(0, 0, 0, 36);
            case Color::OverlayShadow: return rgba(0, 0, 0, mode() == Mode::Dark ? 72 : 42);
            case Color::OverlayHover: return mode() == Mode::Dark ? rgba(255, 255, 255, 18) : rgba(0, 0, 0, 16);
            case Color::AssetExplorerToolbar: return mode() == Mode::Dark ? rgba(35, 35, 35) : rgba(226, 226, 224);
            case Color::AssetExplorerStrip: return mode() == Mode::Dark ? rgba(36, 36, 36) : rgba(236, 236, 233);
            case Color::AssetExplorerTreeBg: return mode() == Mode::Dark ? rgba(36, 36, 36) : rgba(226, 226, 223);
            case Color::AssetExplorerTileBg: return mode() == Mode::Dark ? rgba(42, 42, 42) : rgba(221, 221, 218);
            case Color::AssetExplorerTileBorder: return mode() == Mode::Dark ? rgba(255, 255, 255, 18) : rgba(0, 0, 0, 38);
            case Color::AssetExplorerTileHover: return mode() == Mode::Dark ? rgba(58, 58, 58) : rgba(211, 211, 207);
            case Color::AssetExplorerGridLine: return mode() == Mode::Dark ? rgba(255, 255, 255, 10) : rgba(255, 255, 255, 62);
            case Color::AssetExplorerSeparator: return mode() == Mode::Dark ? rgba(255, 255, 255, 20) : rgba(0, 0, 0, 42);
            case Color::AssetExplorerSelectionRing: return rgba(224, 166, 85, 210);
            case Color::AssetExplorerSelectionSoft: return rgba(224, 166, 85, 36);
            case Color::AssetExplorerSelectionBar: return rgba(224, 166, 85);
            case Color::AssetExplorerBadgeText: return rgba(10, 10, 10);
            case Color::AssetExplorerDetailRow: return mode() == Mode::Dark ? rgba(255, 255, 255, 5) : rgba(255, 255, 255, 90);
            case Color::AssetFolder: return rgba(216, 192, 116);
            case Color::AssetLevel: return rgba(168, 219, 160);
            case Color::AssetMaterial: return rgba(201, 168, 230);
            case Color::AssetModel: return rgba(143, 217, 187);
            case Color::AssetTexture: return rgba(159, 189, 240);
            case Color::AssetCubemap: return rgba(143, 210, 224);
            case Color::AssetAudio: return rgba(230, 168, 192);
            case Color::AssetShader: return rgba(212, 217, 154);
            case Color::AssetScript: return rgba(159, 189, 240);
            case Color::AssetFont: return rgba(232, 202, 143);
            case Color::AssetOther: return rgba(194, 194, 194);
            case Color::AxisX: return rgba(230, 78, 78);
            case Color::AxisY: return rgba(100, 210, 110);
            case Color::AxisZ: return rgba(88, 154, 235);
            case Color::White: return rgba(255, 255, 255);
            case Color::Black: return rgba(0, 0, 0);
        }
        return ImGui::GetStyleColorVec4(ImGuiCol_Text);
    }

    ImU32 EditorTheme::colorU32(const Color color, const float alphaScale) {
        ImVec4 value = EditorTheme::color(color);
        value.w *= alphaScale;
        return ImGui::ColorConvertFloat4ToU32(value);
    }

    float EditorTheme::metric(const Metric metric) {
        switch (metric) {
            case Metric::WindowPaddingX: return 12.0f;
            case Metric::WindowPaddingY: return 10.0f;
            case Metric::FramePaddingX: return 8.0f;
            case Metric::FramePaddingY: return 4.0f;
            case Metric::ItemSpacingX: return 8.0f;
            case Metric::ItemSpacingY: return 5.0f;
            case Metric::ItemInnerSpacingX: return 6.0f;
            case Metric::ItemInnerSpacingY: return 4.0f;
            case Metric::IndentSpacing: return 18.0f;
            case Metric::ScrollbarSize: return 8.0f;
            case Metric::GrabMinSize: return 8.0f;
            case Metric::WindowRounding: return 6.0f;
            case Metric::ChildRounding: return 4.0f;
            case Metric::PopupRounding: return 6.0f;
            case Metric::FrameRounding: return 4.0f;
            case Metric::TabRounding: return 4.0f;
            case Metric::ToolbarIconSize: return 32.0f;
            case Metric::ToolbarButtonWidth: return 36.0f;
            case Metric::ToolbarButtonHeight: return 36.0f;
            case Metric::ToolbarIslandPadding: return 4.0f;
            case Metric::ToolbarIslandRounding: return 9.0f;
            case Metric::ToolbarIslandMargin: return 12.0f;
            case Metric::AssetExplorerToolbarHeight: return 40.0f;
            case Metric::AssetExplorerStripHeight: return 46.0f;
            case Metric::AssetExplorerTreeWidth: return 178.0f;
            case Metric::AssetExplorerTileMin: return 96.0f;
            case Metric::AssetExplorerTileGap: return 12.0f;
            case Metric::AssetExplorerTileLabelHeight: return 36.0f;
            case Metric::AssetExplorerTileRounding: return 8.0f;
            case Metric::AssetExplorerRowRounding: return 5.0f;
        }
        return 0.0f;
    }

    ImVec4 EditorTheme::srgb(const ImVec4 color) {
        auto toLinear = [](const float x) {
            return x <= 0.04045f ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f);
        };
        return ImVec4(toLinear(color.x), toLinear(color.y), toLinear(color.z), color.w);
    }

    ImVec4 EditorTheme::rgba(const int r, const int g, const int b, const int a) {
        return srgb(ImVec4(
            static_cast<float>(r) / 255.0f,
            static_cast<float>(g) / 255.0f,
            static_cast<float>(b) / 255.0f,
            static_cast<float>(a) / 255.0f));
    }

    ScopedColor::ScopedColor(const ImGuiCol target, const EditorTheme::Color color) {
        ImGui::PushStyleColor(target, EditorTheme::color(color));
        active = true;
    }

    ScopedColor::ScopedColor(const ImGuiCol target, const ImVec4 color) {
        ImGui::PushStyleColor(target, color);
        active = true;
    }

    ScopedColor::~ScopedColor() {
        if (active) {
            ImGui::PopStyleColor();
        }
    }

    ScopedStyleVar::ScopedStyleVar(const ImGuiStyleVar target, const float value) {
        ImGui::PushStyleVar(target, value);
        active = true;
    }

    ScopedStyleVar::ScopedStyleVar(const ImGuiStyleVar target, const ImVec2 value) {
        ImGui::PushStyleVar(target, value);
        active = true;
    }

    ScopedStyleVar::~ScopedStyleVar() {
        if (active) {
            ImGui::PopStyleVar();
        }
    }
}
