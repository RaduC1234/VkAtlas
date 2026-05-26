#pragma once

#include "Panel.hpp"
#include "core/EditorHistory.hpp"

#include <Atlas.hpp>

#include <string>

namespace Atlas::Editor {
    class InspectorPanel final : public Panel {
    public:
        InspectorPanel(ProjectLayer &projectLayer, entt::entity &selectedEntity, EditorHistory &history);

        void onImGuiRender() override;

    private:
        void drawEntityHeader(entt::registry &registry);
        void drawTransform(entt::registry &registry);
        void drawModel(entt::registry &registry);
        void drawMaterial(entt::registry &registry);
        void drawLight(entt::registry &registry);
        void drawCamera(entt::registry &registry);
        void drawSkybox(entt::registry &registry);

        bool beginComponent(const char *label);
        void endComponent();

        ProjectLayer &projectLayer;
        entt::entity &selectedEntity;
        EditorHistory &history;

        bool nameEditActive = false;
        std::string nameEditBefore;
        bool transformEditActive = false;
        TransformComponent transformEditBefore;
        bool materialEditActive = false;
        MaterialComponent materialEditBefore;
        bool lightEditActive = false;
        LightComponent lightEditBefore;
    };
}
