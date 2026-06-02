#pragma once

#include "core/EditorHistory.hpp"
#include "Panel.hpp"

#include <Atlas.hpp>
#include <imgui.h>

namespace Atlas::Editor {
    class HierarchyPanel final : public Panel {
    public:
        HierarchyPanel(ProjectLayer &projectLayer, entt::entity &selectedEntity, EditorHistory &history);

        void onImGuiRender() override;
        void deleteSelected();

    private:
        entt::entity createEntity(entt::registry &registry);
        std::string nextEntityName(entt::registry &registry) const;
        void drawEntityNode(entt::registry &registry, entt::entity entity);
        void drawVisibilityButton(entt::registry &registry, entt::entity entity);
        void drawDeleteButton(entt::entity entity);
        void deleteEntity(entt::registry &registry, entt::entity entity);
        static bool containsEntity(entt::registry &registry, entt::entity root, entt::entity entity);
        static void drawEyeIcon(ImDrawList &drawList, const ImVec2 &min, const ImVec2 &max, bool visible, ImU32 color);
        static void drawTrashIcon(ImDrawList &drawList, const ImVec2 &min, const ImVec2 &max, ImU32 color);
        const char *entityName(entt::registry &registry, entt::entity entity) const;

        ProjectLayer &projectLayer;
        entt::entity &selectedEntity;
        EditorHistory &history;
        entt::entity pendingDeleteEntity = entt::null;
    };
}
