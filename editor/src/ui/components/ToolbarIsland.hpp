#pragma once

#include <imgui.h>
#include <functional>

namespace Atlas::Editor {
    class IconRegistry;

    struct ToolbarStyle {
        float iconSize = 0.0f;
        float btnW = 0.0f;
        float btnH = 0.0f;
        float islandPad = 0.0f;
        float islandRounding = 0.0f;
        float islandMargin = 0.0f;
        ImU32 colFill = 0;
        ImU32 colBorder = 0;
        ImU32 colShadow = 0;
        ImU32 colHover = 0;
        ImU32 colAccent = 0;

        static ToolbarStyle defaults();
    };

    class IconButton {
    public:
        IconButton(const char *id, const char *iconName, IconRegistry &icons,
                   ToolbarStyle style = ToolbarStyle::defaults())
            : m_id(id), m_iconName(iconName), m_icons(icons), m_style(style) {
        }

        IconButton &active(bool v) {
            m_active = v;
            return *this;
        }

        IconButton &tooltip(const char *tip) {
            m_tooltip = tip;
            return *this;
        }

        bool render(float &cursorX, float btnY) const;
        bool renderAt(ImVec2 screenPos) const;

    private:
        const char *m_id;
        const char *m_iconName;
        IconRegistry &m_icons;
        ToolbarStyle m_style;
        const char *m_tooltip = nullptr;
        bool m_active = false;

        bool interact(ImVec2 bMin, ImVec2 bMax) const;
        void draw(ImDrawList &dl, ImVec2 bMin, ImVec2 bMax, bool hovered) const;
    };

    class ToolbarIsland {
    public:
        enum class Anchor { TopLeft, TopRight, BottomLeft, BottomRight };

        ToolbarIsland(ImVec2 viewportMin, ImVec2 viewportSize, ToolbarStyle style = ToolbarStyle::defaults())
            : m_vpMin(viewportMin), m_vpSize(viewportSize), m_style(style) {
        }

        ToolbarIsland &anchor(Anchor a, ImVec2 margin = {-1, -1}) {
            m_anchor = a;
            if (margin.x >= 0) m_margin = margin;
            return *this;
        }

        ToolbarIsland &buttons(int count) {
            m_buttonCount = count;
            return *this;
        }

        void render(const std::function<void(float &cursorX, float btnY)> &content) const;

        ImVec2 min() const;
        ImVec2 max() const;
        ImVec2 size() const;

    private:
        ImVec2 m_vpMin;
        ImVec2 m_vpSize;
        ToolbarStyle m_style;
        Anchor m_anchor = Anchor::TopLeft;
        ImVec2 m_margin = {-1, -1}; // -1 means use style.islandMargin
        int m_buttonCount = 1;

        ImVec2 resolvedMargin() const;
        ImVec2 computeMin() const;
    };
} // namespace Atlas::Editor
