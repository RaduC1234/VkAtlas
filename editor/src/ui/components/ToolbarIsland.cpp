#include "ToolbarIsland.hpp"

#include "core/IconRegistry.hpp"
#include "ui/theme/EditorTheme.hpp"

#include <imgui.h>

namespace Atlas::Editor {
    ToolbarStyle ToolbarStyle::defaults() {
        ToolbarStyle style;
        style.iconSize = EditorTheme::metric(EditorTheme::Metric::ToolbarIconSize);
        style.btnW = EditorTheme::metric(EditorTheme::Metric::ToolbarButtonWidth);
        style.btnH = EditorTheme::metric(EditorTheme::Metric::ToolbarButtonHeight);
        style.islandPad = EditorTheme::metric(EditorTheme::Metric::ToolbarIslandPadding);
        style.islandRounding = EditorTheme::metric(EditorTheme::Metric::ToolbarIslandRounding);
        style.islandMargin = EditorTheme::metric(EditorTheme::Metric::ToolbarIslandMargin);
        style.colFill = EditorTheme::colorU32(EditorTheme::Color::OverlayBg);
        style.colBorder = EditorTheme::colorU32(EditorTheme::Color::OverlayBorder);
        style.colShadow = EditorTheme::colorU32(EditorTheme::Color::OverlayShadow);
        style.colHover = EditorTheme::colorU32(EditorTheme::Color::OverlayHover);
        style.colAccent = EditorTheme::colorU32(EditorTheme::Color::Accent);
        return style;
    }

    void IconButton::draw(ImDrawList &dl, const ImVec2 bMin, const ImVec2 bMax, const bool hovered) const {
        // Hover highlight
        if (hovered && !m_active)
            dl.AddRectFilled(bMin, bMax, m_style.colHover, 7.0f);

        // Icon image
        const auto &ic = m_icons.get(m_iconName, static_cast<uint32_t>(m_style.iconSize));
        if (ic.valid()) {
            const ImVec2 sz = ic.size();
            const ImVec2 p(
                bMin.x + (m_style.btnW - sz.x) * 0.5f,
                bMin.y + (m_style.btnH - sz.y) * 0.5f);
            dl.AddImage(ic.textureId(), p, {p.x + sz.x, p.y + sz.y});
        }

        // Active underline
        if (m_active)
            dl.AddLine(
                {bMin.x + 8.0f, bMax.y - 3.0f},
                {bMax.x - 8.0f, bMax.y - 3.0f},
                m_style.colAccent, 2.0f);

        // Tooltip
        if (hovered && m_tooltip)
            ImGui::SetTooltip("%s", m_tooltip);
    }

    bool IconButton::render(float &cursorX, const float btnY) const {
        const ImVec2 bMin(cursorX, btnY);
        const ImVec2 bMax(cursorX + m_style.btnW, btnY + m_style.btnH);

        ImGui::SetCursorScreenPos(bMin);
        ImGui::InvisibleButton(m_id, {m_style.btnW, m_style.btnH});
        const bool hovered = ImGui::IsItemHovered();
        const bool clicked = ImGui::IsItemClicked();

        draw(*ImGui::GetWindowDrawList(), bMin, bMax, hovered);

        cursorX += m_style.btnW;
        return clicked;
    }

    bool IconButton::renderAt(const ImVec2 screenPos) const {
        const ImVec2 bMin = screenPos;
        const ImVec2 bMax(screenPos.x + m_style.btnW, screenPos.y + m_style.btnH);

        ImGui::SetCursorScreenPos(bMin);
        ImGui::InvisibleButton(m_id, {m_style.btnW, m_style.btnH});
        const bool hovered = ImGui::IsItemHovered();
        const bool clicked = ImGui::IsItemClicked();

        draw(*ImGui::GetWindowDrawList(), bMin, bMax, hovered);

        return clicked;
    }

    ImVec2 ToolbarIsland::resolvedMargin() const {
        const float m = m_style.islandMargin;
        return {
            m_margin.x < 0 ? m : m_margin.x,
            m_margin.y < 0 ? m : m_margin.y
        };
    }

    ImVec2 ToolbarIsland::size() const {
        return {
            m_style.islandPad * 2.0f + m_style.btnW * static_cast<float>(m_buttonCount),
            m_style.btnH + m_style.islandPad * 2.0f
        };
    }

    ImVec2 ToolbarIsland::computeMin() const {
        const ImVec2 sz = size();
        const ImVec2 mg = resolvedMargin();
        switch (m_anchor) {
            case Anchor::TopLeft:
                return {m_vpMin.x + mg.x, m_vpMin.y + mg.y};
            case Anchor::TopRight:
                return {m_vpMin.x + m_vpSize.x - sz.x - mg.x, m_vpMin.y + mg.y};
            case Anchor::BottomLeft:
                return {m_vpMin.x + mg.x, m_vpMin.y + m_vpSize.y - sz.y - mg.y};
            case Anchor::BottomRight:
                return {m_vpMin.x + m_vpSize.x - sz.x - mg.x, m_vpMin.y + m_vpSize.y - sz.y - mg.y};
        }
        return {m_vpMin.x + mg.x, m_vpMin.y + mg.y};
    }

    ImVec2 ToolbarIsland::min() const {
        return computeMin();
    }

    ImVec2 ToolbarIsland::max() const {
        const ImVec2 mn = computeMin();
        const ImVec2 sz = size();
        return {mn.x + sz.x, mn.y + sz.y};
    }


    void ToolbarIsland::render(const std::function<void(float &cursorX, float btnY)> &content) const {
        const ImVec2 mn = computeMin();
        const ImVec2 mx = max();
        ImDrawList *dl = ImGui::GetWindowDrawList();

        // Shadow
        dl->AddRectFilled(
            {mn.x, mn.y + 2.0f}, {mx.x, mx.y + 2.0f},
            m_style.colShadow, m_style.islandRounding);
        // Fill
        dl->AddRectFilled(mn, mx, m_style.colFill, m_style.islandRounding);
        // Border
        dl->AddRect(mn, mx, m_style.colBorder, m_style.islandRounding);

        // Suppress ImGui button chrome for all children
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));

        float cursorX = mn.x + m_style.islandPad;
        const float btnY = mn.y + m_style.islandPad;

        content(cursorX, btnY);

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(2);
    }
} // namespace Atlas::Editor
