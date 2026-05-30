#include "ui/widgets/MaterialEditor.hpp"

#include <cstring>
#include <exception>
#include <filesystem>

#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>

#include "core/EditorHistory.hpp"
#include "core/Log.hpp"
#include "utils/FileDialogs.hpp"

namespace Atlas::Editor {
    bool MaterialEditor::drawProperties(
        ProjectLayer &projectLayer,
        EditorHistory &history,
        entt::registry *registry,
        entt::entity ownerEntity,
        AssetHandle<Material> materialHandle,
        MaterialEditState &state
    ) {
        Material *material = materialHandle.get();
        if (!material) {
            ImGui::TextDisabled("No material selected");
            return false;
        }

        const Material before = *material;
        bool changed = false;
        bool finished = false;

        auto beginEdit = [&]() {
            if (!state.active || state.handle != materialHandle) {
                state.active = true;
                state.handle = materialHandle;
                state.before = before;
            }
        };

        char nameBuffer[256]{};
        std::strncpy(nameBuffer, material->name.c_str(), sizeof(nameBuffer) - 1);
        bool itemChanged = ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer));
        changed |= itemChanged;
        if (ImGui::IsItemActivated()) {
            beginEdit();
        }
        if (itemChanged) {
            beginEdit();
            material->name = nameBuffer;
        }
        finished |= ImGui::IsItemDeactivatedAfterEdit();

        constexpr const char *shadingModelNames[] = {
            "Standard PBR",
            "Cloth Charlie",
            "Unlit"
        };
        int shadingModel = static_cast<int>(material->shadingModel);
        itemChanged = ImGui::Combo("Shading", &shadingModel, shadingModelNames, IM_ARRAYSIZE(shadingModelNames));
        changed |= itemChanged;
        if (itemChanged) {
            beginEdit();
            material->shadingModel = static_cast<ShadingModel>(shadingModel);
            finished = true;
        }

        const bool standardPbr = material->shadingModel == ShadingModel::STANDARD_PBR;
        const bool clothCharlie = material->shadingModel == ShadingModel::CLOTH_CHARLIE;
        const bool unlit = material->shadingModel == ShadingModel::UNLIT;

        itemChanged = ImGui::ColorEdit4("Base Color", glm::value_ptr(material->baseColor));
        changed |= itemChanged;
        if (ImGui::IsItemActivated()) {
            beginEdit();
        }
        if (itemChanged) {
            beginEdit();
        }
        finished |= ImGui::IsItemDeactivatedAfterEdit();

        if (standardPbr) {
            itemChanged = ImGui::DragFloat("Metallic", &material->metallic, 0.01f, 0.0f, 1.0f);
            changed |= itemChanged;
            if (ImGui::IsItemActivated()) {
                beginEdit();
            }
            if (itemChanged) {
                beginEdit();
            }
            finished |= ImGui::IsItemDeactivatedAfterEdit();
        }

        if (!unlit) {
            itemChanged = ImGui::DragFloat("Roughness", &material->roughness, 0.01f, 0.04f, 1.0f);
            changed |= itemChanged;
            if (ImGui::IsItemActivated()) {
                beginEdit();
            }
            if (itemChanged) {
                beginEdit();
            }
            finished |= ImGui::IsItemDeactivatedAfterEdit();
        }

        constexpr const char *alphaModeNames[] = {
            "Opaque",
            "Alpha Masked",
            "Transparent"
        };
        int alphaMode = static_cast<int>(material->alphaMode);
        itemChanged = ImGui::Combo("Alpha Mode", &alphaMode, alphaModeNames, IM_ARRAYSIZE(alphaModeNames));
        changed |= itemChanged;
        if (itemChanged) {
            beginEdit();
            material->alphaMode = static_cast<AlphaMode>(alphaMode);
            finished = true;
        }

        if (material->alphaMode == AlphaMode::MASK) {
            itemChanged = ImGui::DragFloat("Alpha Cutoff", &material->alphaCutoff, 0.01f, 0.0f, 1.0f);
            changed |= itemChanged;
            if (ImGui::IsItemActivated()) {
                beginEdit();
            }
            if (itemChanged) {
                beginEdit();
            }
            finished |= ImGui::IsItemDeactivatedAfterEdit();
        }

        if (clothCharlie) {
            itemChanged = ImGui::DragFloat("Sheen", &material->sheenStrength, 0.01f, 0.0f, 1.0f);
            changed |= itemChanged;
            if (ImGui::IsItemActivated()) {
                beginEdit();
            }
            if (itemChanged) {
                beginEdit();
            }
            finished |= ImGui::IsItemDeactivatedAfterEdit();

            itemChanged = ImGui::ColorEdit3("Sheen Color", glm::value_ptr(material->sheenColor));
            changed |= itemChanged;
            if (ImGui::IsItemActivated()) {
                beginEdit();
            }
            if (itemChanged) {
                beginEdit();
            }
            finished |= ImGui::IsItemDeactivatedAfterEdit();
        }

        itemChanged = ImGui::ColorEdit3("Emission", glm::value_ptr(material->emissiveColor));
        changed |= itemChanged;
        if (ImGui::IsItemActivated()) {
            beginEdit();
        }
        if (itemChanged) {
            beginEdit();
        }
        finished |= ImGui::IsItemDeactivatedAfterEdit();

        itemChanged = ImGui::DragFloat("Emission Strength", &material->emissiveStrength, 0.1f, 0.0f, 100.0f);
        changed |= itemChanged;
        if (ImGui::IsItemActivated()) {
            beginEdit();
        }
        if (itemChanged) {
            beginEdit();
        }
        finished |= ImGui::IsItemDeactivatedAfterEdit();

        ImGui::Separator();
        if (drawTextureSlot(projectLayer, "Albedo", material->baseColorTexture)) {
            beginEdit();
            changed = true;
            finished = true;
        }

        if (!unlit && drawTextureSlot(projectLayer, "Normal", material->normalTexture)) {
            beginEdit();
            changed = true;
            finished = true;
        }

        if (standardPbr && drawTextureSlot(projectLayer, "Metallic/Roughness", material->metallicRoughnessTexture)) {
            beginEdit();
            changed = true;
            finished = true;
        }

        if (!unlit && drawTextureSlot(projectLayer, "AO", material->occlusionTexture)) {
            beginEdit();
            changed = true;
            finished = true;
        }

        if (drawTextureSlot(projectLayer, "Emissive", material->emissiveTexture)) {
            beginEdit();
            changed = true;
            finished = true;
        }

        if (changed) {
            if (!state.active) {
                beginEdit();
            }
            patchMaterialUsers(registry, materialHandle);
        }

        if (state.active && state.handle == materialHandle && finished) {
            history.recordMaterialAsset(ownerEntity, materialHandle, state.before, *material);
            state.active = false;
            state.handle = {};
        }

        return changed;
    }

    std::string MaterialEditor::displayName(const std::string &path, AssetHandle<Material> materialHandle) {
        if (Material *material = materialHandle.get()) {
            if (!material->name.empty()) {
                return material->name;
            }
        }

        const std::string displayPath = path.empty() && materialHandle.hasPath() ? materialHandle.path() : path;
        const std::filesystem::path materialPath(displayPath);
        const std::string filename = materialPath.filename().string();
        if (!filename.empty()) {
            return filename;
        }

        return displayPath.empty() ? "Unnamed Material" : displayPath;
    }

    bool MaterialEditor::drawTextureSlot(ProjectLayer &projectLayer, const char *label, AssetHandle<Texture> &texture) {
        bool changed = false;

        ImGui::PushID(label);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine(160.0f);
        ImGui::TextUnformatted(texture.valid() ? (texture.isReady() ? "Assigned" : "Loading") : "None");
        ImGui::SameLine();

        if (ImGui::Button("Select")) {
            ImGui::OpenPopup("Texture Picker");
        }

        if (ImGui::BeginPopup("Texture Picker")) {
            const auto paths = projectLayer.assetManager().assetPaths<Texture>();
            bool hasProjectTexture = false;

            for (const auto &path: paths) {
                if (isInternalTexturePath(path)) {
                    continue;
                }
                hasProjectTexture = true;
                break;
            }

            if (!hasProjectTexture) {
                ImGui::TextDisabled("No project textures");
            } else {
                for (const auto &path: paths) {
                    if (isInternalTexturePath(path)) {
                        continue;
                    }

                    const std::string displayName = textureDisplayName(path);

                    ImGui::PushID(path.c_str());
                    if (ImGui::Selectable(displayName.c_str())) {
                        texture = projectLayer.assetManager().find<Texture>(path);
                        changed = texture.valid();
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", path.c_str());
                    }
                    ImGui::PopID();
                }
            }

            ImGui::Separator();
            if (ImGui::Selectable("Import Texture...")) {
                const std::string filter = buildTextureFilter();
                const std::string path = FileDialogs::openFile(filter.c_str());
                if (!path.empty()) {
                    try {
                        texture = projectLayer.assetManager().store<Texture>(path);
                        changed = true;
                    } catch (const std::exception &error) {
                        AT_ERROR("MaterialEditor: failed to load texture '{}': {}", path, error.what());
                    }
                }
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
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

    void MaterialEditor::patchMaterialUsers(entt::registry *registry, AssetHandle<Material> materialHandle) {
        if (!registry) {
            return;
        }

        auto view = registry->view<MaterialComponent>();
        for (const entt::entity entity: view) {
            if (view.get<MaterialComponent>(entity).materialHandle == materialHandle) {
                registry->patch<MaterialComponent>(entity);
            }
        }
    }

    bool MaterialEditor::isInternalTexturePath(const std::string &path) {
        return path.starts_with("##engine/") || path.starts_with("##engine\\") ||
               path.starts_with("##editor/") || path.starts_with("##editor\\");
    }

    std::string MaterialEditor::textureDisplayName(const std::string &path) {
        const std::filesystem::path texturePath(path);
        const std::string filename = texturePath.filename().string();
        return filename.empty() ? path : filename;
    }

    std::string MaterialEditor::buildTextureFilter() {
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
}
