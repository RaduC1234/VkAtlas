#pragma once

#include <Atlas.hpp>

#include <memory>
#include <string>
#include <vector>

#include "panels/HierarchyPanel.hpp"
#include "panels/InspectorPanel.hpp"
#include "panels/ViewportPanel.hpp"
#include "panels/RenderSettingsPanel.hpp"

namespace Atlas::Editor {
    class EditorLayer final : public Layer {
    public:
        explicit EditorLayer(ProjectLayer &projectLayer);
        void onAttach() override;
        void onDetach() override;
        void onUpdate(float deltaTime) override;
        void onImGuiRender() override;

    private:
        void drawMenuBar();
        void importIntoLevel();
        static std::string buildImportFilter(const std::vector<std::string> &extensions);
        static bool hasSupportedExtension(const std::string &path, const std::vector<std::string> &extensions);

        ProjectLayer &projectLayer;
        entt::entity selectedEntity = entt::null;

        std::shared_ptr<HierarchyPanel> hierarchyPanel;
        std::shared_ptr<InspectorPanel> inspectorPanel;
        std::shared_ptr<ViewportPanel> viewportPanel;
        std::shared_ptr<RenderSettingsPanel> renderSettingsPanel;
    };
}
