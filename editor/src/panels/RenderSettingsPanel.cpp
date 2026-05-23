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

        auto *scene = projectLayer.project().scene();
        if (!scene) {
            ImGui::Text("No scene loaded");
            ImGui::End();
            return;
        }

        auto &debugData = scene->debugData();
        int mode = static_cast<int>(debugData.viewMode);
        constexpr const char *modes[] = {
            "Lit",
            "Unlit",
            "Lighting Only",
            "Path Tracing"
        };

        if (ImGui::Combo("Mode", &mode, modes, IM_ARRAYSIZE(modes))) {
            debugData.viewMode = static_cast<ViewMode>(mode);
        }

        ImGui::DragFloat("Exposure", &debugData.exposureMultiplier, 0.01f, 0.0f, 10.0f);
        ImGui::DragFloat("Irradiance", &debugData.irradianceMultiplier, 0.01f, 0.0f, 10.0f);

        ImGui::End();
    }
}
