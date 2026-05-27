#include "InspectorPanel.hpp"

#include <cstring>
#include <string>
#include <utility>

#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>

#include "widgets/MaterialEditor.hpp"

namespace Atlas::Editor {
    InspectorPanel::InspectorPanel(
        ProjectLayer &projectLayer,
        entt::entity &selectedEntity,
        EditorHistory &history,
        OpenMaterialEditorCallback openMaterialEditor
    )
        : projectLayer(projectLayer),
          selectedEntity(selectedEntity),
          history(history),
          openMaterialEditor(std::move(openMaterialEditor)) {
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

        if (registry.all_of<SkyboxComponent>(selectedEntity)) {
            drawSkybox(registry);
        }

        ImGui::End();
    }

    // -------------------------------------------------------------------------

    void InspectorPanel::drawEntityHeader(entt::registry &registry) {
        if (!registry.all_of<SceneNodeComponent>(selectedEntity)) return;
        auto &node = registry.get<SceneNodeComponent>(selectedEntity);
        const std::string before = node.name;

        char buf[256]{};
        std::strncpy(buf, node.name.c_str(), sizeof(buf) - 1);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##entityname", buf, sizeof(buf))) {
            if (!nameEditActive) {
                nameEditActive = true;
                nameEditBefore = before;
            }
            node.name = buf;
        }

        if (nameEditActive && ImGui::IsItemDeactivatedAfterEdit()) {
            history.recordName(selectedEntity, nameEditBefore, node.name);
            nameEditActive = false;
        }
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
        const TransformComponent before = t;
        glm::vec3 degrees = glm::degrees(t.rotation);
        bool changed = false;
        bool finished = false;

        changed |= ImGui::DragFloat3("Position", glm::value_ptr(t.translation), 0.01f);
        if (ImGui::IsItemActivated() && !transformEditActive) {
            transformEditActive = true;
            transformEditBefore = before;
        }
        finished |= ImGui::IsItemDeactivatedAfterEdit();

        changed |= ImGui::DragFloat3("Rotation", glm::value_ptr(degrees), 0.1f);
        if (ImGui::IsItemActivated() && !transformEditActive) {
            transformEditActive = true;
            transformEditBefore = before;
        }
        finished |= ImGui::IsItemDeactivatedAfterEdit();

        changed |= ImGui::DragFloat3("Scale", glm::value_ptr(t.scale), 0.01f);
        if (ImGui::IsItemActivated() && !transformEditActive) {
            transformEditActive = true;
            transformEditBefore = before;
        }
        finished |= ImGui::IsItemDeactivatedAfterEdit();

        if (changed) {
            if (!transformEditActive) {
                transformEditActive = true;
                transformEditBefore = before;
            }

            t.rotation = glm::radians(degrees);
            registry.patch<TransformComponent>(selectedEntity);
        }

        if (transformEditActive && finished) {
            history.recordTransform(selectedEntity, transformEditBefore, t);
            transformEditActive = false;
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

        auto &component = registry.get<MaterialComponent>(selectedEntity);
        const std::string currentName = component.materialHandle.valid()
                                            ? MaterialEditor::displayName({}, component.materialHandle)
                                            : "None";

        if (ImGui::BeginCombo("Material", currentName.c_str())) {
            const auto paths = projectLayer.assetManager().assetPaths<Material>();
            if (paths.empty()) {
                ImGui::TextDisabled("No project materials");
            }

            for (const auto &path: paths) {
                AssetHandle<Material> materialHandle = projectLayer.assetManager().find<Material>(path);
                if (!materialHandle.valid()) {
                    continue;
                }

                const std::string displayName = MaterialEditor::displayName(path, materialHandle);
                const bool selected = materialHandle == component.materialHandle;

                ImGui::PushID(path.c_str());
                if (ImGui::Selectable(displayName.c_str(), selected)) {
                    const MaterialComponent before = component;
                    component.materialHandle = materialHandle;
                    registry.patch<MaterialComponent>(selectedEntity);
                    history.recordMaterial(selectedEntity, before, component);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", path.c_str());
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
                ImGui::PopID();
            }

            ImGui::EndCombo();
        }

        ImGui::LabelText("Status", "%s", component.materialHandle.valid() ? "Assigned" : "None");

        if (!component.materialHandle.valid()) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Open")) {
            openMaterialEditor(selectedEntity, component.materialHandle);
        }
        if (!component.materialHandle.valid()) {
            ImGui::EndDisabled();
        }

        endComponent();
    }

    void InspectorPanel::drawLight(entt::registry &registry) {
        if (!beginComponent("Light")) return;

        auto &light = registry.get<LightComponent>(selectedEntity);
        const LightComponent before = light;
        bool changed = false;
        bool finished = false;

        constexpr const char *types[] = {"Unknown", "Point", "Spot", "Directional", "Rectangle"};
        int type = static_cast<int>(light.type);
        if (ImGui::Combo("Type", &type, types, IM_ARRAYSIZE(types))) {
            if (!lightEditActive) {
                lightEditActive = true;
                lightEditBefore = before;
            }
            light.type = static_cast<LightType>(type);
            changed = true;
            finished = true;
        }

        bool itemChanged = ImGui::ColorEdit3("Color", glm::value_ptr(light.color));
        changed |= itemChanged;
        if (ImGui::IsItemActivated() && !lightEditActive) {
            lightEditActive = true;
            lightEditBefore = before;
        }
        finished |= ImGui::IsItemDeactivatedAfterEdit();

        itemChanged = ImGui::DragFloat("Intensity", &light.intensity, 0.1f, 0.f, 100.f);
        changed |= itemChanged;
        if (ImGui::IsItemActivated() && !lightEditActive) {
            lightEditActive = true;
            lightEditBefore = before;
        }
        finished |= ImGui::IsItemDeactivatedAfterEdit();

        itemChanged = ImGui::DragFloat("Range", &light.range, 1.0f, 0.f, 500.f);
        changed |= itemChanged;
        if (ImGui::IsItemActivated() && !lightEditActive) {
            lightEditActive = true;
            lightEditBefore = before;
        }
        finished |= ImGui::IsItemDeactivatedAfterEdit();

        if (light.type == LightType::SPOT) {
            float inner = glm::degrees(light.innerConeAngle);
            float outer = glm::degrees(light.outerConeAngle);
            if (ImGui::DragFloat("Inner Angle", &inner, 0.1f, 0.f, 90.f)) {
                if (!lightEditActive) {
                    lightEditActive = true;
                    lightEditBefore = before;
                }
                light.innerConeAngle = glm::radians(inner);
                changed = true;
            }
            finished |= ImGui::IsItemDeactivatedAfterEdit();

            if (ImGui::DragFloat("Outer Angle", &outer, 0.1f, 0.f, 90.f)) {
                if (!lightEditActive) {
                    lightEditActive = true;
                    lightEditBefore = before;
                }
                light.outerConeAngle = glm::radians(outer);
                changed = true;
            }
            finished |= ImGui::IsItemDeactivatedAfterEdit();
        }

        if (changed) {
            if (!lightEditActive) {
                lightEditActive = true;
                lightEditBefore = before;
            }
            registry.patch<LightComponent>(selectedEntity);
        }

        if (lightEditActive && finished) {
            history.recordLight(selectedEntity, lightEditBefore, light);
            lightEditActive = false;
        }

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

    void InspectorPanel::drawSkybox(entt::registry &registry) {
        if (!beginComponent("Skybox")) return;

        if (auto *node = registry.try_get<SceneNodeComponent>(selectedEntity)) {
            bool enabled = node->visible;
            if (ImGui::Checkbox("Enabled", &enabled)) {
                const bool before = node->visible;
                registry.patch<SceneNodeComponent>(selectedEntity, [&](auto &sceneNode) {
                    sceneNode.visible = enabled;
                });
                history.recordVisibility(selectedEntity, before, enabled);
            }
        }

        const auto &skybox = registry.get<SkyboxComponent>(selectedEntity);
        const auto status = [](const auto &handle) {
            if (!handle.valid()) return "None";
            return handle.isReady() ? "Ready" : "Loading";
        };
        ImGui::LabelText("Skybox", "%s", status(skybox.skyboxHandle));
        ImGui::LabelText("Irradiance", "%s", status(skybox.irradianceHandle));
        ImGui::LabelText("Prefilter", "%s", status(skybox.prefilterHandle));

        endComponent();
    }
}
