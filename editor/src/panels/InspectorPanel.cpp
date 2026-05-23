#include "InspectorPanel.hpp"

#include <cstring>

#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>

namespace Atlas::Editor {
    InspectorPanel::InspectorPanel(ProjectLayer &projectLayer, entt::entity &selectedEntity)
        : projectLayer(projectLayer), selectedEntity(selectedEntity) {
    }

    void InspectorPanel::onImGuiRender() {
        if (!visible) {
            return;
        }

        ImGui::Begin("Properties", &visible);

        if (selectedEntity == entt::null) {
            ImGui::TextDisabled("No entity selected");
            ImGui::End();
            return;
        }

        auto *scene = projectLayer.project().scene();
        if (!scene) {
            ImGui::End();
            return;
        }
        auto &registry = scene->getRegistry();

        if (!registry.valid(selectedEntity)) {
            selectedEntity = entt::null;
            ImGui::End();
            return;
        }

        drawEntityHeader(registry);
        ImGui::Separator();

        if (registry.all_of<TransformComponent>(selectedEntity)) {
            drawTransform(registry);
        }

        if (registry.all_of<ModelComponent>(selectedEntity)) {
            drawModel(registry);
        }

        if (registry.all_of<MaterialComponent>(selectedEntity)) {
            drawMaterial(registry);
        }

        if (registry.all_of<LightComponent>(selectedEntity)) {
            drawLight(registry);
        }

        if (registry.all_of<CameraComponent>(selectedEntity)) {
            drawCamera(registry);
        }

        ImGui::End();
    }

    // -------------------------------------------------------------------------

    void InspectorPanel::drawEntityHeader(entt::registry &registry) {
        if (!registry.all_of<SceneNodeComponent>(selectedEntity)) return;
        auto &node = registry.get<SceneNodeComponent>(selectedEntity);

        char buf[256]{};
        std::strncpy(buf, node.name.c_str(), sizeof(buf) - 1);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##entityname", buf, sizeof(buf)))
            node.name = buf;
    }

    bool InspectorPanel::beginComponent(const char *label) {
        constexpr ImGuiTreeNodeFlags flags =
                ImGuiTreeNodeFlags_DefaultOpen |
                ImGuiTreeNodeFlags_Framed |
                ImGuiTreeNodeFlags_SpanAvailWidth |
                ImGuiTreeNodeFlags_FramePadding;

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 4));
        bool open = ImGui::TreeNodeEx(label, flags);
        ImGui::PopStyleVar();

        if (open) ImGui::Spacing();
        return open;
    }

    void InspectorPanel::endComponent() {
        ImGui::Spacing();
        ImGui::TreePop();
    }

    // -------------------------------------------------------------------------

    void InspectorPanel::drawTransform(entt::registry &registry) {
        if (!beginComponent("Transform")) return;

        auto &t = registry.get<TransformComponent>(selectedEntity);
        glm::vec3 degrees = glm::degrees(t.rotation);
        bool changed = false;

        changed |= ImGui::DragFloat3("Position", glm::value_ptr(t.translation), 0.01f);
        changed |= ImGui::DragFloat3("Rotation", glm::value_ptr(degrees), 0.1f);
        changed |= ImGui::DragFloat3("Scale", glm::value_ptr(t.scale), 0.01f);

        if (changed) {
            t.rotation = glm::radians(degrees);
            registry.patch<TransformComponent>(selectedEntity);
        }

        endComponent();
    }

    void InspectorPanel::drawModel(entt::registry &registry) {
        if (!beginComponent("Model")) return;

        auto &model = registry.get<ModelComponent>(selectedEntity);
        ImGui::LabelText("Mesh", "%s", model.meshHandle.valid() ? "Assigned" : "None");
        ImGui::LabelText("Status", "%s", model.meshHandle.isReady() ? "Ready" : "Loading");

        endComponent();
    }

    void InspectorPanel::drawMaterial(entt::registry &registry) {
        if (!beginComponent("Material")) return;

        auto &mat = registry.get<MaterialComponent>(selectedEntity);
        bool changed = false;

        changed |= ImGui::ColorEdit4("Base Color", glm::value_ptr(mat.baseColor));

        changed |= ImGui::Checkbox("Alpha Masked", &mat.alphaMasked);
        changed |= ImGui::Checkbox("Transparent", &mat.transparent);

        ImGui::LabelText("Albedo", "%s", mat.albedoTexture.valid() ? "Assigned" : "None");
        ImGui::LabelText("Normal", "%s", mat.normalMap.valid() ? "Assigned" : "None");
        ImGui::LabelText("Metallic/Roughness", "%s", mat.metallicRoughnessMap.valid() ? "Assigned" : "None");
        ImGui::LabelText("AO", "%s", mat.ambientOcclusion.valid() ? "Assigned" : "None");

        if (changed)
            registry.patch<MaterialComponent>(selectedEntity);

        endComponent();
    }

    void InspectorPanel::drawLight(entt::registry &registry) {
        if (!beginComponent("Light")) return;

        auto &light = registry.get<LightComponent>(selectedEntity);
        bool changed = false;

        constexpr const char *types[] = {"Unknown", "Point", "Spot", "Directional", "Rectangle"};
        int type = static_cast<int>(light.type);
        if (ImGui::Combo("Type", &type, types, IM_ARRAYSIZE(types))) {
            light.type = static_cast<LightType>(type);
            changed = true;
        }

        changed |= ImGui::ColorEdit3("Color", glm::value_ptr(light.color));
        changed |= ImGui::DragFloat("Intensity", &light.intensity, 0.1f, 0.f, 100.f);
        changed |= ImGui::DragFloat("Range", &light.range, 1.0f, 0.f, 500.f);

        if (light.type == LightType::SPOT) {
            float inner = glm::degrees(light.innerConeAngle);
            float outer = glm::degrees(light.outerConeAngle);
            if (ImGui::DragFloat("Inner Angle", &inner, 0.1f, 0.f, 90.f)) {
                light.innerConeAngle = glm::radians(inner);
                changed = true;
            }
            if (ImGui::DragFloat("Outer Angle", &outer, 0.1f, 0.f, 90.f)) {
                light.outerConeAngle = glm::radians(outer);
                changed = true;
            }
        }

        if (changed)
            registry.patch<LightComponent>(selectedEntity);

        endComponent();
    }

    void InspectorPanel::drawCamera(entt::registry &registry) {
        if (!beginComponent("Camera")) return;

        const auto &cam = registry.get<CameraComponent>(selectedEntity);
        const auto data = cam.camera.getData();

        ImGui::LabelText("Near", "%.3f", data.nearPlane);
        ImGui::LabelText("Far", "%.3f", data.farPlane);
        endComponent();
    }
}
