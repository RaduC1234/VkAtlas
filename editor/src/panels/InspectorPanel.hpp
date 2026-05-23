#pragma once

#include "Panel.hpp"

#include <Atlas.hpp>

namespace Atlas::Editor {
    class InspectorPanel final : public Panel {
    public:
        InspectorPanel(ProjectLayer &projectLayer, entt::entity &selectedEntity);

        void onImGuiRender() override;

    private:
        void drawEntityHeader(entt::registry &registry);
        void drawTransform(entt::registry &registry);
        void drawModel(entt::registry &registry);
        void drawMaterial(entt::registry &registry);
        void drawLight(entt::registry &registry);
        void drawCamera(entt::registry &registry);

        bool beginComponent(const char *label);
        void endComponent();

        ProjectLayer &projectLayer;
        entt::entity &selectedEntity;
    };
}
