#pragma once

#include "Panel.hpp"

#include <Atlas.hpp>

namespace Atlas::Editor {
    class HierarchyPanel final : public Panel {
    public:
        HierarchyPanel(ProjectLayer &projectLayer, entt::entity &selectedEntity);

        void onImGuiRender() override;

    private:
        void drawEntityNode(entt::registry &registry, entt::entity entity);
        const char *entityName(entt::registry &registry, entt::entity entity) const;

        ProjectLayer &projectLayer;
        entt::entity &selectedEntity;
    };
}
