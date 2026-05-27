#include "InspectorPanel.hpp"

#include <cstring>
#include <exception>
#include <string>

#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>

#include "core/Log.hpp"
#include "utils/FileDialogs.hpp"

namespace Atlas::Editor {
    InspectorPanel::InspectorPanel(ProjectLayer &projectLayer, entt::entity &selectedEntity, EditorHistory &history)
        : projectLayer(projectLayer), selectedEntity(selectedEntity), history(history) {
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

    bool InspectorPanel::drawTextureSlot(const char *label, AssetHandle<Texture> &texture) {
        bool changed = false;

        ImGui::PushID(label);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine(160.0f);
        ImGui::TextUnformatted(texture.valid() ? (texture.isReady() ? "Assigned" : "Loading") : "None");
        ImGui::SameLine();

        if (ImGui::Button("Select")) {
            const std::string filter = buildTextureFilter();
            const std::string path = FileDialogs::openFile(filter.c_str());
            if (!path.empty()) {
                try {
                    texture = projectLayer.assetManager().store<Texture>(path);
                    changed = true;
                } catch (const std::exception &error) {
                    AT_ERROR("InspectorPanel: failed to load texture '{}': {}", path, error.what());
                }
            }
        }

        ImGui::SameLine();
        const bool hadTexture = texture.valid();
        if (!hadTexture) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Clear")) {
            texture = AssetHandle<Texture>::invalid();
            changed = true;
        }
        if (!hadTexture) {
            ImGui::EndDisabled();
        }

        ImGui::PopID();
        return changed;
    }

    std::string InspectorPanel::buildTextureFilter() {
        std::string filter = "Image Files (*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.hdr;*.ktx;*.ktx2)";
        filter.push_back('\0');
        filter += "*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.hdr;*.ktx;*.ktx2";
        filter.push_back('\0');
        filter += "All Files (*.*)";
        filter.push_back('\0');
        filter += "*.*";
        filter.push_back('\0');
        filter.push_back('\0');
        return filter;
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
        Material *mat = component.materialHandle.get();
        if (!mat) {
            ImGui::LabelText("Material", "None");
            endComponent();
            return;
        }

        const Material before = *mat;
        bool changed = false;
        bool finished = false;

        auto beginMaterialEdit = [&]() {
            if (!materialEditActive || materialEditHandle != component.materialHandle) {
                materialEditActive = true;
                materialEditHandle = component.materialHandle;
                materialEditBefore = before;
            }
        };

        char nameBuffer[256]{};
        std::strncpy(nameBuffer, mat->name.c_str(), sizeof(nameBuffer) - 1);
        bool itemChanged = ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer));
        changed |= itemChanged;
        if (ImGui::IsItemActivated()) {
            beginMaterialEdit();
        }
        if (itemChanged) {
            beginMaterialEdit();
            mat->name = nameBuffer;
        }
        finished |= ImGui::IsItemDeactivatedAfterEdit();

        constexpr const char *shadingModelNames[] = {
            "Standard PBR",
            "Cloth Charlie",
            "Unlit"
        };
        int shadingModel = static_cast<int>(mat->shadingModel);
        itemChanged = ImGui::Combo("Shading", &shadingModel, shadingModelNames, IM_ARRAYSIZE(shadingModelNames));
        changed |= itemChanged;
        if (itemChanged) {
            beginMaterialEdit();
            mat->shadingModel = static_cast<ShadingModel>(shadingModel);
            finished = true;
        }

        const bool standardPbr = mat->shadingModel == ShadingModel::STANDARD_PBR;
        const bool clothCharlie = mat->shadingModel == ShadingModel::CLOTH_CHARLIE;
        const bool unlit = mat->shadingModel == ShadingModel::UNLIT;

        itemChanged = ImGui::ColorEdit4("Base Color", glm::value_ptr(mat->baseColor));
        changed |= itemChanged;
        if (ImGui::IsItemActivated()) {
            beginMaterialEdit();
        }
        if (itemChanged) {
            beginMaterialEdit();
        }
        finished |= ImGui::IsItemDeactivatedAfterEdit();

        if (standardPbr) {
            itemChanged = ImGui::DragFloat("Metallic", &mat->metallic, 0.01f, 0.0f, 1.0f);
            changed |= itemChanged;
            if (ImGui::IsItemActivated()) {
                beginMaterialEdit();
            }
            if (itemChanged) {
                beginMaterialEdit();
            }
            finished |= ImGui::IsItemDeactivatedAfterEdit();
        }

        if (!unlit) {
            itemChanged = ImGui::DragFloat("Roughness", &mat->roughness, 0.01f, 0.04f, 1.0f);
            changed |= itemChanged;
            if (ImGui::IsItemActivated()) {
                beginMaterialEdit();
            }
            if (itemChanged) {
                beginMaterialEdit();
            }
            finished |= ImGui::IsItemDeactivatedAfterEdit();
        }

        constexpr const char *alphaModeNames[] = {
            "Opaque",
            "Alpha Masked",
            "Transparent"
        };
        int alphaMode = static_cast<int>(mat->alphaMode);
        itemChanged = ImGui::Combo("Alpha Mode", &alphaMode, alphaModeNames, IM_ARRAYSIZE(alphaModeNames));
        changed |= itemChanged;
        if (itemChanged) {
            beginMaterialEdit();
            mat->alphaMode = static_cast<AlphaMode>(alphaMode);
            finished = true;
        }

        if (mat->alphaMode == AlphaMode::MASK) {
            itemChanged = ImGui::DragFloat("Alpha Cutoff", &mat->alphaCutoff, 0.01f, 0.0f, 1.0f);
            changed |= itemChanged;
            if (ImGui::IsItemActivated()) {
                beginMaterialEdit();
            }
            if (itemChanged) {
                beginMaterialEdit();
            }
            finished |= ImGui::IsItemDeactivatedAfterEdit();
        }

        if (clothCharlie) {
            itemChanged = ImGui::DragFloat("Sheen", &mat->sheenStrength, 0.01f, 0.0f, 1.0f);
            changed |= itemChanged;
            if (ImGui::IsItemActivated()) {
                beginMaterialEdit();
            }
            if (itemChanged) {
                beginMaterialEdit();
            }
            finished |= ImGui::IsItemDeactivatedAfterEdit();

            itemChanged = ImGui::ColorEdit3("Sheen Color", glm::value_ptr(mat->sheenColor));
            changed |= itemChanged;
            if (ImGui::IsItemActivated()) {
                beginMaterialEdit();
            }
            if (itemChanged) {
                beginMaterialEdit();
            }
            finished |= ImGui::IsItemDeactivatedAfterEdit();
        }

        itemChanged = ImGui::ColorEdit3("Emission", glm::value_ptr(mat->emissiveColor));
        changed |= itemChanged;
        if (ImGui::IsItemActivated()) {
            beginMaterialEdit();
        }
        if (itemChanged) {
            beginMaterialEdit();
        }
        finished |= ImGui::IsItemDeactivatedAfterEdit();

        itemChanged = ImGui::DragFloat("Emission Strength", &mat->emissiveStrength, 0.1f, 0.0f, 100.0f);
        changed |= itemChanged;
        if (ImGui::IsItemActivated()) {
            beginMaterialEdit();
        }
        if (itemChanged) {
            beginMaterialEdit();
        }
        finished |= ImGui::IsItemDeactivatedAfterEdit();

        ImGui::Separator();
        if (drawTextureSlot("Albedo", mat->baseColorTexture)) {
            beginMaterialEdit();
            changed = true;
            finished = true;
        }

        if (!unlit) {
            if (drawTextureSlot("Normal", mat->normalTexture)) {
                beginMaterialEdit();
                changed = true;
                finished = true;
            }
        }

        if (standardPbr) {
            if (drawTextureSlot("Metallic/Roughness", mat->metallicRoughnessTexture)) {
                beginMaterialEdit();
                changed = true;
                finished = true;
            }
        }

        if (!unlit) {
            if (drawTextureSlot("AO", mat->occlusionTexture)) {
                beginMaterialEdit();
                changed = true;
                finished = true;
            }
        }

        if (drawTextureSlot("Emissive", mat->emissiveTexture)) {
            beginMaterialEdit();
            changed = true;
            finished = true;
        }

        if (changed) {
            if (!materialEditActive) {
                beginMaterialEdit();
            }
            registry.patch<MaterialComponent>(selectedEntity);
        }

        if (materialEditActive && materialEditHandle == component.materialHandle && finished) {
            history.recordMaterialAsset(selectedEntity, component.materialHandle, materialEditBefore, *mat);
            materialEditActive = false;
            materialEditHandle = {};
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
