#pragma once

#include "Panel.hpp"
#include "core/EditorHistory.hpp"

#include <Atlas.hpp>

#include <functional>
#include <string>

namespace Atlas::Editor {
    class InspectorPanel final : public Panel {
    public:
        using OpenMaterialEditorCallback = std::function<void(entt::entity, AssetHandle<Material>)>;

        InspectorPanel(
            ProjectLayer &projectLayer,
            entt::entity &selectedEntity,
            EditorHistory &history,
            OpenMaterialEditorCallback openMaterialEditor
        );

        void onImGuiRender() override;

    private:
        void drawEntityHeader(entt::registry &registry);
        void drawAddComponentMenu(entt::registry &registry);
        void drawTransform(entt::registry &registry);
        void drawModel(entt::registry &registry);
        void drawMaterial(entt::registry &registry);
        void drawLight(entt::registry &registry);
        void drawCamera(entt::registry &registry);
        void drawSkybox(entt::registry &registry);
        void drawPostProcessingVolume(entt::registry &registry);

        bool beginComponent(const char *label, bool *removeRequested = nullptr);
        void endComponent();

        ProjectLayer &projectLayer;
        entt::entity &selectedEntity;
        EditorHistory &history;
        OpenMaterialEditorCallback openMaterialEditor;

        bool nameEditActive = false;
        std::string nameEditBefore;
        bool transformEditActive = false;
        TransformComponent transformEditBefore;
        bool lightEditActive = false;
        LightComponent lightEditBefore;
    };
}
