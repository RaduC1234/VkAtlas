#include "RenderSettingsPanel.hpp"

#include <imgui.h>

namespace Atlas::Editor {
    RenderSettingsPanel::RenderSettingsPanel(ProjectLayer &projectLayer) : projectLayer(projectLayer) {
    }

    void RenderSettingsPanel::onImGuiRender() {
        if (!visible) {
            return;
        }

        ImGui::Begin("Render Settings", &visible);
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);

        auto &rendererSettings = projectLayer.getRenderer().settings();
        int mode = static_cast<int>(rendererSettings.viewMode);
        constexpr const char *modes[] = {
            "Lit",
            "Unlit",
            "Lighting Only",
            "Path Tracing"
        };

        if (ImGui::Combo("Mode", &mode, modes, IM_ARRAYSIZE(modes))) {
            rendererSettings.viewMode = static_cast<ViewMode>(mode);
        }

        auto *scene = projectLayer.project().scene();
        if (!scene) {
            ImGui::Text("No scene loaded");
            ImGui::End();
            return;
        }

        auto &debugData = scene->debugData();
        ImGui::DragFloat("Exposure", &debugData.exposureMultiplier, 0.01f, 0.0f, 10.0f);
        ImGui::DragFloat("Irradiance", &debugData.irradianceMultiplier, 0.01f, 0.0f, 10.0f);

        ImGui::End();
    }
}
