#include "ViewportPanel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#define IMVIEWGUIZMO_IMPLEMENTATION
#include <ImViewGuizmo.h>

namespace Atlas::Editor::ViewportGizmo {
    glm::vec3 meshLocalCenter(const Mesh &mesh) {
        const auto &vertices = mesh.vertices();
        if (vertices.empty()) {
            return glm::vec3{0.0f};
        }

        glm::vec3 minPosition = vertices.front().position;
        glm::vec3 maxPosition = vertices.front().position;
        for (const auto &vertex: vertices) {
            minPosition = glm::min(minPosition, vertex.position);
            maxPosition = glm::max(maxPosition, vertex.position);
        }

        return (minPosition + maxPosition) * 0.5f;
    }

    glm::quat rotationYXZ(const glm::vec3 &rotation) {
        return glm::angleAxis(rotation.y, glm::vec3{0.0f, 1.0f, 0.0f}) *
               glm::angleAxis(rotation.x, glm::vec3{1.0f, 0.0f, 0.0f}) *
               glm::angleAxis(rotation.z, glm::vec3{0.0f, 0.0f, 1.0f});
    }

    glm::vec3 eulerFromRotationYXZ(const glm::quat &rotation) {
        const glm::mat3 matrix = glm::mat3_cast(rotation);
        const float x = std::asin(glm::clamp(-matrix[2][1], -1.0f, 1.0f));
        const float cosX = std::cos(x);

        float y = 0.0f;
        float z = 0.0f;
        if (std::abs(cosX) > 0.0001f) {
            y = std::atan2(matrix[2][0], matrix[2][2]);
            z = std::atan2(matrix[0][1], matrix[1][1]);
        } else {
            y = std::atan2(-matrix[0][2], matrix[0][0]);
        }

        return {x, glm::mod(y, glm::two_pi<float>()), z};
    }

    ImViewGuizmo::TransformOperation toImViewOperation(const ObjectGizmoMode mode) {
        switch (mode) {
            case ObjectGizmoMode::Translate:
                return ImViewGuizmo::TRANSFORM_TRANSLATE;
            case ObjectGizmoMode::Rotate:
                return ImViewGuizmo::TRANSFORM_ROTATE;
            case ObjectGizmoMode::Scale:
                return ImViewGuizmo::TRANSFORM_SCALE;
        }

        return ImViewGuizmo::TRANSFORM_TRANSLATE;
    }

    bool projectWorldToViewport(
        const glm::mat4 &viewProjection,
        const glm::vec3 &worldPosition,
        const ImVec2 imageMin,
        const ImVec2 imageSize,
        ImVec2 &screenPosition) {
        const glm::vec4 clip = viewProjection * glm::vec4(worldPosition, 1.0f);
        if (clip.w <= 0.0001f) {
            return false;
        }

        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (ndc.x < -1.0f || ndc.x > 1.0f || ndc.y < -1.0f || ndc.y > 1.0f || ndc.z < 0.0f || ndc.z > 1.0f) {
            return false;
        }

        screenPosition = {
            imageMin.x + (ndc.x * 0.5f + 0.5f) * imageSize.x,
            imageMin.y + (ndc.y * 0.5f + 0.5f) * imageSize.y
        };
        return true;
    }

    bool projectBillboardCorner(
        const glm::mat4 &viewProjection,
        const glm::vec3 &worldPosition,
        const ImVec2 imageMin,
        const ImVec2 imageSize,
        ImVec2 &screenPosition) {
        const glm::vec4 clip = viewProjection * glm::vec4(worldPosition, 1.0f);
        if (clip.w <= 0.0001f) {
            return false;
        }

        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        screenPosition = {
            imageMin.x + (ndc.x * 0.5f + 0.5f) * imageSize.x,
            imageMin.y + (ndc.y * 0.5f + 0.5f) * imageSize.y
        };
        return true;
    }

    ImU32 lightIconColor(const LightComponent &light) {
        const glm::vec3 color = glm::clamp(light.color, glm::vec3{0.0f}, glm::vec3{1.0f});
        return IM_COL32(
            static_cast<int>(color.r * 255.0f),
            static_cast<int>(color.g * 255.0f),
            static_cast<int>(color.b * 255.0f),
            255);
    }

    const char *lightTypeName(const LightType type) {
        switch (type) {
            case LightType::POINT:
                return "Point Light";
            case LightType::SPOT:
                return "Spot Light";
            case LightType::DIRECTIONAL:
                return "Directional Light";
            case LightType::RECT:
                return "Rect Light";
            case LightType::UNKNOWN:
                break;
        }

        return "Light";
    }

    bool projectLightBillboard(
        const Camera::Data &cameraData,
        const glm::vec3 &worldPosition,
        const ImVec2 imageMin,
        const ImVec2 imageSize,
        const float worldSize,
        std::array<ImVec2, 4> &corners,
        ImVec2 &screenCenter) {
        const glm::mat4 inverseView = glm::inverse(cameraData.view);
        const glm::vec3 cameraRight = glm::normalize(glm::vec3(inverseView * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)));
        const glm::vec3 cameraUp = glm::normalize(glm::vec3(inverseView * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)));
        const float halfSize = worldSize * 0.5f;

        const std::array<glm::vec3, 4> worldCorners = {
            worldPosition + (-cameraRight + cameraUp) * halfSize,
            worldPosition + ( cameraRight + cameraUp) * halfSize,
            worldPosition + ( cameraRight - cameraUp) * halfSize,
            worldPosition + (-cameraRight - cameraUp) * halfSize,
        };

        for (size_t i = 0; i < worldCorners.size(); ++i) {
            if (!projectBillboardCorner(cameraData.viewProjection, worldCorners[i], imageMin, imageSize, corners[i])) {
                return false;
            }
        }

        return projectWorldToViewport(cameraData.viewProjection, worldPosition, imageMin, imageSize, screenCenter);
    }

    void drawLightBillboardIcon(ImDrawList &drawList, const std::array<ImVec2, 4> &corners, const ImVec2 center, const LightComponent &light, const bool selected) {
        const ImU32 fill = lightIconColor(light);
        const ImU32 fillDim = (fill & IM_COL32(255, 255, 255, 0)) | IM_COL32(0, 0, 0, 130);
        const ImU32 outline = selected ? IM_COL32(255, 255, 255, 255) : IM_COL32(20, 20, 20, 230);
        const float thickness = selected ? 2.2f : 1.4f;

        drawList.AddQuadFilled(
            ImVec2(corners[0].x + 1.0f, corners[0].y + 1.0f),
            ImVec2(corners[1].x + 1.0f, corners[1].y + 1.0f),
            ImVec2(corners[2].x + 1.0f, corners[2].y + 1.0f),
            ImVec2(corners[3].x + 1.0f, corners[3].y + 1.0f),
            IM_COL32(0, 0, 0, 95));
        drawList.AddQuadFilled(corners[0], corners[1], corners[2], corners[3], fillDim);
        drawList.AddQuad(corners[0], corners[1], corners[2], corners[3], outline, thickness);

        const ImVec2 top((corners[0].x + corners[1].x) * 0.5f, (corners[0].y + corners[1].y) * 0.5f);
        const ImVec2 right((corners[1].x + corners[2].x) * 0.5f, (corners[1].y + corners[2].y) * 0.5f);
        const ImVec2 bottom((corners[2].x + corners[3].x) * 0.5f, (corners[2].y + corners[3].y) * 0.5f);
        const ImVec2 left((corners[3].x + corners[0].x) * 0.5f, (corners[3].y + corners[0].y) * 0.5f);

        drawList.AddQuadFilled(top, right, bottom, left, fill);
        drawList.AddQuad(top, right, bottom, left, outline, 1.2f);

        if (light.type == LightType::SPOT) {
            drawList.AddLine(top, bottom, outline, 1.4f);
            drawList.AddLine(left, right, outline, 1.4f);
        } else if (light.type == LightType::DIRECTIONAL) {
            drawList.AddLine(left, right, outline, 1.6f);
            drawList.AddLine(right, top, outline, 1.6f);
            drawList.AddLine(right, bottom, outline, 1.6f);
        } else if (light.type == LightType::RECT) {
            drawList.AddQuad(corners[0], corners[1], corners[2], corners[3], fill, 1.2f);
        }
    }
}

namespace Atlas::Editor {
    ViewportPanel::ViewportPanel(ProjectLayer &projectLayer, entt::entity &selectedEntity, EditorHistory &history)
        : projectLayer(projectLayer), selectedEntity(selectedEntity), history(history) {
    }

    ViewportPanel::~ViewportPanel() {
        destroyViewportTexture();
    }

    void ViewportPanel::onDetach() {
        destroyViewportTexture();
    }

    void ViewportPanel::onImGuiRender() {
        if (!visible) {
            return;
        }

        if (viewportTexture == VK_NULL_HANDLE || !ImViewGuizmo::IsUsing()) {
            createViewportTexture();
        }

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("Viewport", &visible);
        ImGui::PopStyleVar();

        renderToolbar();

        const ImVec2 size = ImGui::GetContentRegionAvail();
        if (size.x > 1.0f && size.y > 1.0f) {
            projectLayer.getRenderer().setSceneViewportExtent({
                static_cast<uint32_t>(size.x),
                static_cast<uint32_t>(size.y)
            });
        }

        if (viewportTexture != VK_NULL_HANDLE && size.x > 1.0f && size.y > 1.0f) {
            const ImVec2 imageMin = ImGui::GetCursorScreenPos();
            ImGui::Image((ImTextureID) viewportTexture, size);
            const bool viewportHovered = ImGui::IsItemHovered();
            ImViewGuizmo::BeginFrame();
            renderLightBillboards(imageMin, size);
            renderObjectGizmo(imageMin, size, viewportHovered);
            renderViewGizmo(imageMin, size);
            renderContextMenu(viewportHovered && !ImViewGuizmo::IsOver() && !ImViewGuizmo::IsUsing());
        }

        ImGui::End();
    }

    void ViewportPanel::renderToolbar() {
        constexpr float toolbarHeight = 20.0f;
        constexpr float comboWidth = 132.0f;
        constexpr const char *modeNames[] = {
            "Lit",
            "Unlit",
            "Lighting Only",
            "Path Tracing"
        };

        const ImVec2 toolbarMin = ImGui::GetCursorScreenPos();
        const ImVec2 toolbarSize = ImVec2(ImGui::GetContentRegionAvail().x, toolbarHeight);
        const ImVec2 toolbarMax = ImVec2(toolbarMin.x + toolbarSize.x, toolbarMin.y + toolbarSize.y);
        ImGui::GetWindowDrawList()->AddRectFilled(toolbarMin, toolbarMax, IM_COL32(32, 32, 32, 255));

        ImGui::BeginChild("##viewport_toolbar", toolbarSize, false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        auto &settings = projectLayer.getRenderer().settings();
        int mode = static_cast<int>(settings.viewMode);

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 1.0f));
        const float y = (toolbarHeight - ImGui::GetFrameHeight()) * 0.5f;
        ImGui::SetCursorPos(ImVec2(8.0f, y));
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);

        constexpr const char *gizmoModeNames[] = {
            "Translate",
            "Rotate",
            "Scale"
        };
        int gizmoMode = static_cast<int>(objectGizmoMode);
        ImGui::SetCursorPosY(y);
        ImGui::SetCursorPosX(92.0f);
        ImGui::SetNextItemWidth(104.0f);
        if (ImGui::Combo("##viewport_gizmo_mode", &gizmoMode, gizmoModeNames, IM_ARRAYSIZE(gizmoModeNames))) {
            objectGizmoMode = static_cast<ObjectGizmoMode>(gizmoMode);
        }

        constexpr const char *gizmoSpaceNames[] = {
            "Local",
            "World"
        };
        int gizmoSpace = static_cast<int>(objectGizmoSpace);
        ImGui::SetCursorPosY(y);
        ImGui::SetCursorPosX(204.0f);
        ImGui::SetNextItemWidth(84.0f);
        if (ImGui::Combo("##viewport_gizmo_space", &gizmoSpace, gizmoSpaceNames, IM_ARRAYSIZE(gizmoSpaceNames))) {
            objectGizmoSpace = static_cast<ObjectGizmoSpace>(gizmoSpace);
        }

        const float comboX = toolbarSize.x > comboWidth + 16.0f
            ? toolbarSize.x - comboWidth - 8.0f
            : 8.0f;
        ImGui::SetCursorPosY(y);
        ImGui::SetCursorPosX(comboX);
        ImGui::SetNextItemWidth(comboWidth);

        if (ImGui::Combo("##viewport_render_mode", &mode, modeNames, IM_ARRAYSIZE(modeNames))) {
            settings.viewMode = static_cast<ViewMode>(mode);
        }
        ImGui::PopStyleVar();

        ImGui::EndChild();
    }

    void ViewportPanel::renderContextMenu(const bool viewportHovered) {
        if (viewportHovered && ImGui::Shortcut(ImGuiMod_Shift | ImGuiKey_A)) {
            ImGui::OpenPopup("##viewport_context_menu");
        }

        if (ImGui::BeginPopup("##viewport_context_menu")) {
            if (ImGui::BeginMenu("Add")) {
                if (ImGui::MenuItem("Cube")) {
                    addPrimitive(ViewportPrimitive::Cube);
                    ImGui::CloseCurrentPopup();
                }

                if (ImGui::MenuItem("Square")) {
                    addPrimitive(ViewportPrimitive::Square);
                    ImGui::CloseCurrentPopup();
                }

                if (ImGui::MenuItem("Sphere")) {
                    addPrimitive(ViewportPrimitive::Sphere);
                    ImGui::CloseCurrentPopup();
                }

                if (ImGui::BeginMenu("Lights")) {
                    if (ImGui::MenuItem("Point")) {
                        addLight(LightType::POINT);
                        ImGui::CloseCurrentPopup();
                    }

                    if (ImGui::MenuItem("Spot")) {
                        addLight(LightType::SPOT);
                        ImGui::CloseCurrentPopup();
                    }

                    if (ImGui::MenuItem("Directional")) {
                        addLight(LightType::DIRECTIONAL);
                        ImGui::CloseCurrentPopup();
                    }

                    if (ImGui::MenuItem("Rectangle")) {
                        addLight(LightType::RECT);
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::EndMenu();
                }

                ImGui::EndMenu();
            }

            ImGui::EndPopup();
        }
    }

    void ViewportPanel::renderLightBillboards(const ImVec2 imageMin, const ImVec2 imageSize) {
        auto *scene = projectLayer.project().scene();
        if (!scene) {
            return;
        }

        auto &registry = scene->getRegistry();
        auto cameraView = registry.view<TransformComponent, CameraComponent>();
        if (cameraView.begin() == cameraView.end()) {
            return;
        }

        const entt::entity cameraEntity = *cameraView.begin();
        const auto cameraData = registry.get<CameraComponent>(cameraEntity).camera.getData();
        ImDrawList *drawList = ImGui::GetWindowDrawList();
        const ImVec2 restoreCursor = ImGui::GetCursorScreenPos();

        auto lightView = registry.view<TransformComponent, LightComponent>();
        for (const entt::entity entity: lightView) {
            if (const auto *node = registry.try_get<SceneNodeComponent>(entity); node && (node->deleted || !node->visible)) {
                continue;
            }

            const auto &transform = lightView.get<TransformComponent>(entity);
            const auto &light = lightView.get<LightComponent>(entity);

            std::array<ImVec2, 4> corners{};
            ImVec2 screenPosition{};
            const float billboardSize = selectedEntity == entity ? 0.42f : 0.32f;
            if (!ViewportGizmo::projectLightBillboard(cameraData, transform.translation, imageMin, imageSize, billboardSize, corners, screenPosition)) {
                continue;
            }

            ImVec2 hitMin = corners[0];
            ImVec2 hitMax = corners[0];
            for (const ImVec2 &corner: corners) {
                hitMin.x = std::min(hitMin.x, corner.x);
                hitMin.y = std::min(hitMin.y, corner.y);
                hitMax.x = std::max(hitMax.x, corner.x);
                hitMax.y = std::max(hitMax.y, corner.y);
            }

            const float minHitSize = 14.0f;
            if (hitMax.x - hitMin.x < minHitSize) {
                const float expand = (minHitSize - (hitMax.x - hitMin.x)) * 0.5f;
                hitMin.x -= expand;
                hitMax.x += expand;
            }
            if (hitMax.y - hitMin.y < minHitSize) {
                const float expand = (minHitSize - (hitMax.y - hitMin.y)) * 0.5f;
                hitMin.y -= expand;
                hitMax.y += expand;
            }

            ImGui::SetCursorScreenPos(hitMin);
            ImGui::PushID(static_cast<int>(entt::to_integral(entity)));
            const bool clicked = ImGui::InvisibleButton("light_billboard", ImVec2(hitMax.x - hitMin.x, hitMax.y - hitMin.y));
            const bool hovered = ImGui::IsItemHovered();
            ImGui::PopID();

            if (clicked) {
                selectedEntity = entity;
            }

            ViewportGizmo::drawLightBillboardIcon(*drawList, corners, screenPosition, light, selectedEntity == entity);

            if (hovered) {
                if (const auto *node = registry.try_get<SceneNodeComponent>(entity); node && !node->name.empty()) {
                    ImGui::SetTooltip("%s", node->name.c_str());
                } else {
                    ImGui::SetTooltip("%s", ViewportGizmo::lightTypeName(light.type));
                }
            }
        }

        ImGui::SetCursorScreenPos(restoreCursor);
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    }

    void ViewportPanel::renderObjectGizmo(const ImVec2 imageMin, const ImVec2 imageSize, const bool viewportHovered) {
        (void)viewportHovered;

        auto *scene = projectLayer.project().scene();
        if (!scene) {
            return;
        }

        auto &registry = scene->getRegistry();
        if (selectedEntity == entt::null || !registry.valid(selectedEntity) || !registry.all_of<TransformComponent>(selectedEntity)) {
            return;
        }

        auto cameraView = registry.view<TransformComponent, CameraComponent>();
        if (cameraView.begin() == cameraView.end()) {
            return;
        }

        const entt::entity cameraEntity = *cameraView.begin();
        const auto &cameraComponent = registry.get<CameraComponent>(cameraEntity);
        const auto cameraData = cameraComponent.camera.getData();

        auto &transform = registry.get<TransformComponent>(selectedEntity);
        const TransformComponent beforeTransform = transform;
        glm::vec3 gizmoWorldPosition = transform.translation;
        if (const auto *model = registry.try_get<ModelComponent>(selectedEntity)) {
            if (const Mesh *mesh = model->meshHandle.get()) {
                const glm::vec3 localCenter = ViewportGizmo::meshLocalCenter(*mesh);
                gizmoWorldPosition = glm::vec3(transform.mat4() * glm::vec4(localCenter, 1.0f));
            }
        }

        glm::vec3 editablePosition = gizmoWorldPosition;
        glm::quat editableRotation = ViewportGizmo::rotationYXZ(transform.rotation);
        const glm::quat gizmoOrientation = objectGizmoSpace == ObjectGizmoSpace::Local
            ? editableRotation
            : glm::quat{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 editableScale = transform.scale;
        const glm::vec3 previousGizmoPosition = editablePosition;

        auto &style = ImViewGuizmo::GetStyle();
        style.scale = 1.0f;
        style.transformAxisLength = 54.0f;
        style.transformRotateRadius = 48.0f;

        const bool modified = ImViewGuizmo::Transform(
            cameraData.viewProjection,
            imageMin,
            imageSize,
            ViewportGizmo::toImViewOperation(objectGizmoMode),
            editablePosition,
            editableRotation,
            editableScale,
            gizmoOrientation);

        if (modified) {
            if (!objectTransformEditActive || objectTransformEditEntity != selectedEntity) {
                objectTransformEditActive = true;
                objectTransformEditEntity = selectedEntity;
                objectTransformEditBefore = beforeTransform;
            }

            if (objectGizmoMode == ObjectGizmoMode::Translate) {
                transform.translation += editablePosition - previousGizmoPosition;
            } else if (objectGizmoMode == ObjectGizmoMode::Rotate) {
                transform.rotation = ViewportGizmo::eulerFromRotationYXZ(editableRotation);
            } else if (objectGizmoMode == ObjectGizmoMode::Scale) {
                transform.scale = editableScale;
            }

            registry.patch<TransformComponent>(selectedEntity);

            if (registry.all_of<CameraComponent>(selectedEntity)) {
                registry.patch<CameraComponent>(selectedEntity, [&](auto &cc) {
                    cc.camera.setViewYXZ(transform.translation, transform.rotation);
                });
            }
        }

        if (objectTransformEditActive && objectTransformEditEntity == selectedEntity && !ImViewGuizmo::IsTransformUsing()) {
            history.recordTransform(selectedEntity, objectTransformEditBefore, transform);
            objectTransformEditActive = false;
            objectTransformEditEntity = entt::null;
        }
    }

    void ViewportPanel::renderViewGizmo(ImVec2 imageMin, ImVec2 imageSize) {
        auto *scene = projectLayer.project().scene();
        if (!scene || imageSize.x < 160.0f || imageSize.y < 160.0f) {
            return;
        }

        auto view = scene->getRegistry().view<TransformComponent, CameraComponent>();
        if (view.begin() == view.end()) {
            return;
        }

        const entt::entity cameraEntity = *view.begin();
        const auto &transform = view.get<TransformComponent>(cameraEntity);

        const float pitch = transform.rotation.x;
        const float yaw   = transform.rotation.y;
        glm::vec3 forward = glm::normalize(glm::vec3{
            std::cos(pitch) * std::sin(yaw),
            -std::sin(pitch),
            std::cos(pitch) * std::cos(yaw)
        });

        glm::vec3 worldUp{0.0f, 1.0f, 0.0f};
        if (std::abs(forward.y) > 0.99f)
            worldUp = {1.0f, 0.0f, 0.0f};

        const glm::vec3 right = glm::normalize(glm::cross(worldUp, forward));
        const glm::vec3 up    = glm::cross(right, forward);

        const glm::vec3 fixedCameraPosition = transform.translation;

        // Only reseed the persisted rotation when the gizmo is idle.
        // While IsUsing() is true (axis snap animation in progress), the gizmo
        // owns cameraRotation and interpolates it across frames. Rebuilding it
        // from euler angles each frame resets the slerp source and breaks the
        // animation, causing the camera to jump to a wrong orientation.
        if (!ImViewGuizmo::IsUsing()) {
            cameraRotation = glm::quatLookAt(forward, up);
        }

        constexpr float gizmoScale       = 0.7f;
        constexpr float gizmoPadding     = 14.0f;
        constexpr float gizmoHalfExtent  = 128.0f * gizmoScale;

        auto &style           = ImViewGuizmo::GetStyle();
        style.scale           = gizmoScale;
        style.lineWidth       = 4.0f;
        style.circleRadius    = 18.0f;
        style.highlightWidth  = 3.0f;
        style.bigCircleRadius = 94.0f;
        style.labelSize       = 1.1f;

        const ImVec2 gizmoScreenPos{
            imageMin.x + imageSize.x - gizmoHalfExtent - gizmoPadding,
            imageMin.y + gizmoHalfExtent + gizmoPadding
        };

        glm::vec3 cameraPosition = fixedCameraPosition;
        if (ImViewGuizmo::Rotate(cameraPosition, cameraRotation, fixedCameraPosition, gizmoScreenPos, 0.008f)) {
            const glm::vec3 updatedForward = glm::normalize(cameraRotation * ImViewGuizmo::worldForward);

            scene->getRegistry().patch<TransformComponent>(cameraEntity, [&](auto &tc) {
                tc.translation = fixedCameraPosition;
                tc.rotation.x  = std::asin(glm::clamp(-updatedForward.y, -1.0f, 1.0f));
                tc.rotation.y  = glm::mod(std::atan2(updatedForward.x, updatedForward.z), glm::two_pi<float>());
                tc.rotation.z  = 0.0f;
            });

            scene->getRegistry().patch<CameraComponent>(cameraEntity, [&](auto &cc) {
                const auto &tc = scene->getRegistry().get<TransformComponent>(cameraEntity);
                cc.camera.setViewYXZ(tc.translation, tc.rotation);
            });
        }

        if (ImViewGuizmo::IsOver() || ImViewGuizmo::IsUsing()) {
            ImGui::SetNextFrameWantCaptureMouse(true);
        }
    }

    void ViewportPanel::addPrimitive(const ViewportPrimitive primitive) {
        auto *scene = projectLayer.project().scene();
        if (!scene) {
            return;
        }

        auto &registry = scene->getRegistry();
        const entt::entity entity = registry.create();

        SceneNodeComponent node{};
        node.name = primitiveName(primitive);
        registry.emplace<SceneNodeComponent>(entity, std::move(node));

        TransformComponent transform{};
        transform.translation = primitiveSpawnPosition();
        registry.emplace<TransformComponent>(entity, transform);

        ModelComponent model{};
        model.meshHandle = primitiveMesh(primitive);
        registry.emplace<ModelComponent>(entity, model);

        MaterialComponent material{};
        auto materialAsset = std::make_shared<Material>();
        materialAsset->name = primitiveName(primitive) + " Material";
        materialAsset->baseColor = glm::vec4{0.82f, 0.82f, 0.78f, 1.0f};
        materialAsset->baseColorTexture = primitiveWhiteTexture();
        const std::string materialPath = "##editor/materials/" + materialAsset->name;
        material.materialHandle = projectLayer.assetManager().store<Material>(
            std::move(materialAsset),
            materialPath);
        registry.emplace<MaterialComponent>(entity, material);

        selectedEntity = entity;
    }

    void ViewportPanel::addLight(const LightType type) {
        auto *scene = projectLayer.project().scene();
        if (!scene) {
            return;
        }

        auto &registry = scene->getRegistry();
        const entt::entity entity = registry.create();

        SceneNodeComponent node{};
        node.name = lightName(type);
        registry.emplace<SceneNodeComponent>(entity, std::move(node));

        TransformComponent transform{};
        transform.translation = primitiveSpawnPosition();
        registry.emplace<TransformComponent>(entity, transform);

        LightComponent light{};
        light.type = type;
        if (type == LightType::RECT) {
            light.width = 1.0f;
            light.height = 1.0f;
        }
        registry.emplace<LightComponent>(entity, light);

        selectedEntity = entity;
    }

    AssetHandle<Mesh> ViewportPanel::primitiveMesh(const ViewportPrimitive primitive) {
        const auto makeVertex = [](const glm::vec3 position, const glm::vec3 normal, const glm::vec2 uv, const glm::vec4 tangent) {
            Mesh::Vertex vertex{};
            vertex.position = position;
            vertex.color = glm::vec3{1.0f};
            vertex.normal = normal;
            vertex.uv = uv;
            vertex.tangent = tangent;
            return vertex;
        };

        std::vector<Mesh::Vertex> vertices;
        std::vector<uint32_t> indices;

        if (primitive == ViewportPrimitive::Cube) {
            vertices.reserve(24);
            indices.reserve(36);

            const auto addFace = [&](const glm::vec3 a, const glm::vec3 b, const glm::vec3 c, const glm::vec3 d, const glm::vec3 normal, const glm::vec4 tangent) {
                const uint32_t base = static_cast<uint32_t>(vertices.size());
                vertices.push_back(makeVertex(a, normal, {0.0f, 0.0f}, tangent));
                vertices.push_back(makeVertex(b, normal, {1.0f, 0.0f}, tangent));
                vertices.push_back(makeVertex(c, normal, {1.0f, 1.0f}, tangent));
                vertices.push_back(makeVertex(d, normal, {0.0f, 1.0f}, tangent));

                indices.push_back(base + 0);
                indices.push_back(base + 1);
                indices.push_back(base + 2);
                indices.push_back(base + 0);
                indices.push_back(base + 2);
                indices.push_back(base + 3);
            };

            constexpr float halfExtent = 0.5f;
            const glm::vec3 left{-halfExtent, 0.0f, 0.0f};
            const glm::vec3 right{halfExtent, 0.0f, 0.0f};
            const glm::vec3 bottom{0.0f, -halfExtent, 0.0f};
            const glm::vec3 top{0.0f, halfExtent, 0.0f};
            const glm::vec3 back{0.0f, 0.0f, -halfExtent};
            const glm::vec3 front{0.0f, 0.0f, halfExtent};

            addFace(left + bottom + front, right + bottom + front, right + top + front, left + top + front, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f});
            addFace(right + bottom + back, left + bottom + back, left + top + back, right + top + back, {0.0f, 0.0f, -1.0f}, {-1.0f, 0.0f, 0.0f, 1.0f});
            addFace(right + bottom + front, right + bottom + back, right + top + back, right + top + front, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f, 1.0f});
            addFace(left + bottom + back, left + bottom + front, left + top + front, left + top + back, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f});
            addFace(left + top + front, right + top + front, right + top + back, left + top + back, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f});
            addFace(left + bottom + back, right + bottom + back, right + bottom + front, left + bottom + front, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f});

            return projectLayer.assetManager().store<Mesh>(std::make_shared<Mesh>(vertices, indices), "##editor/primitives/cube");
        }

        if (primitive == ViewportPrimitive::Sphere) {
            constexpr uint32_t segments = 32;
            constexpr uint32_t rings = 16;
            constexpr float radius = 0.5f;

            vertices.reserve((segments + 1) * (rings + 1));
            indices.reserve(segments * rings * 6);

            for (uint32_t ring = 0; ring <= rings; ++ring) {
                const float v = static_cast<float>(ring) / static_cast<float>(rings);
                const float theta = v * glm::pi<float>();
                const float sinTheta = std::sin(theta);
                const float cosTheta = std::cos(theta);

                for (uint32_t segment = 0; segment <= segments; ++segment) {
                    const float u = static_cast<float>(segment) / static_cast<float>(segments);
                    const float phi = u * glm::two_pi<float>();
                    const float sinPhi = std::sin(phi);
                    const float cosPhi = std::cos(phi);

                    glm::vec3 normal{sinTheta * cosPhi, cosTheta, sinTheta * sinPhi};
                    glm::vec3 position = normal * radius;
                    glm::vec4 tangent{-sinPhi, 0.0f, cosPhi, 1.0f};
                    vertices.push_back(makeVertex(position, normal, {u, v}, tangent));
                }
            }

            for (uint32_t ring = 0; ring < rings; ++ring) {
                for (uint32_t segment = 0; segment < segments; ++segment) {
                    const uint32_t a = ring * (segments + 1) + segment;
                    const uint32_t b = (ring + 1) * (segments + 1) + segment;
                    const uint32_t c = (ring + 1) * (segments + 1) + segment + 1;
                    const uint32_t d = ring * (segments + 1) + segment + 1;

                    indices.push_back(a);
                    indices.push_back(d);
                    indices.push_back(c);
                    indices.push_back(a);
                    indices.push_back(c);
                    indices.push_back(b);
                }
            }

            return projectLayer.assetManager().store<Mesh>(std::make_shared<Mesh>(vertices, indices), "##editor/primitives/sphere");
        }

        vertices.reserve(8);
        indices.reserve(12);
        const glm::vec3 topNormal{0.0f, 1.0f, 0.0f};
        const glm::vec3 bottomNormal{0.0f, -1.0f, 0.0f};
        const glm::vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};
        vertices.push_back(makeVertex({-0.5f, 0.0f, 0.5f}, topNormal, {0.0f, 0.0f}, tangent));
        vertices.push_back(makeVertex({0.5f, 0.0f, 0.5f}, topNormal, {1.0f, 0.0f}, tangent));
        vertices.push_back(makeVertex({0.5f, 0.0f, -0.5f}, topNormal, {1.0f, 1.0f}, tangent));
        vertices.push_back(makeVertex({-0.5f, 0.0f, -0.5f}, topNormal, {0.0f, 1.0f}, tangent));
        vertices.push_back(makeVertex({-0.5f, 0.0f, 0.5f}, bottomNormal, {0.0f, 0.0f}, tangent));
        vertices.push_back(makeVertex({-0.5f, 0.0f, -0.5f}, bottomNormal, {0.0f, 1.0f}, tangent));
        vertices.push_back(makeVertex({0.5f, 0.0f, -0.5f}, bottomNormal, {1.0f, 1.0f}, tangent));
        vertices.push_back(makeVertex({0.5f, 0.0f, 0.5f}, bottomNormal, {1.0f, 0.0f}, tangent));
        indices = {0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};

        return projectLayer.assetManager().store<Mesh>(std::make_shared<Mesh>(vertices, indices), "##editor/primitives/square_twosided");
    }

    AssetHandle<Texture> ViewportPanel::primitiveWhiteTexture() {
        return projectLayer.assetManager().store<Texture>(Texture::default_(), "##editor/primitives/white");
    }

    glm::vec3 ViewportPanel::primitiveSpawnPosition() {
        auto *scene = projectLayer.project().scene();
        if (!scene) {
            return glm::vec3{0.0f};
        }

        auto view = scene->getRegistry().view<TransformComponent, CameraComponent>();
        if (view.begin() == view.end()) {
            return glm::vec3{0.0f};
        }

        const entt::entity cameraEntity = *view.begin();
        const auto &transform = view.get<TransformComponent>(cameraEntity);
        const float pitch = transform.rotation.x;
        const float yaw = transform.rotation.y;
        const glm::vec3 forward = glm::normalize(glm::vec3{
            std::cos(pitch) * std::sin(yaw),
            -std::sin(pitch),
            std::cos(pitch) * std::cos(yaw)
        });

        return transform.translation + forward * 3.0f;
    }

    std::string ViewportPanel::primitiveName(const ViewportPrimitive primitive) {
        auto *scene = projectLayer.project().scene();
        const char *baseName = "Primitive";
        switch (primitive) {
            case ViewportPrimitive::Cube:
                baseName = "Cube";
                break;
            case ViewportPrimitive::Square:
                baseName = "Square";
                break;
            case ViewportPrimitive::Sphere:
                baseName = "Sphere";
                break;
        }

        if (!scene) {
            return baseName;
        }

        auto &registry = scene->getRegistry();
        auto nameExists = [&](const std::string &name) {
            for (const entt::entity entity: registry.view<SceneNodeComponent>()) {
                const auto &node = registry.get<SceneNodeComponent>(entity);
                if (!node.deleted && node.name == name) {
                    return true;
                }
            }

            return false;
        };

        if (!nameExists(baseName)) {
            return baseName;
        }

        char suffix[8]{};
        for (uint32_t index = 1; index < 1000; ++index) {
            std::snprintf(suffix, sizeof(suffix), ".%03u", index);
            std::string candidate = std::string(baseName) + suffix;
            if (!nameExists(candidate)) {
                return candidate;
            }
        }

        return std::string(baseName) + ".999";
    }

    std::string ViewportPanel::lightName(const LightType type) {
        auto *scene = projectLayer.project().scene();
        const char *baseName = "Light";
        switch (type) {
            case LightType::POINT:
                baseName = "Point Light";
                break;
            case LightType::SPOT:
                baseName = "Spot Light";
                break;
            case LightType::DIRECTIONAL:
                baseName = "Directional Light";
                break;
            case LightType::RECT:
                baseName = "Rectangle Light";
                break;
            case LightType::UNKNOWN:
                break;
        }

        if (!scene) {
            return baseName;
        }

        auto &registry = scene->getRegistry();
        auto nameExists = [&](const std::string &name) {
            for (const entt::entity entity: registry.view<SceneNodeComponent>()) {
                const auto &node = registry.get<SceneNodeComponent>(entity);
                if (!node.deleted && node.name == name) {
                    return true;
                }
            }

            return false;
        };

        if (!nameExists(baseName)) {
            return baseName;
        }

        char suffix[8]{};
        for (uint32_t index = 1; index < 1000; ++index) {
            std::snprintf(suffix, sizeof(suffix), ".%03u", index);
            std::string candidate = std::string(baseName) + suffix;
            if (!nameExists(candidate)) {
                return candidate;
            }
        }

        return std::string(baseName) + ".999";
    }

    void ViewportPanel::createViewportTexture() {
        const auto &outputImage = projectLayer.getRenderer().getSceneOutputImage();
        if (!outputImage.valid()) {
            destroyViewportTexture();
            return;
        }

        if (viewportTexture != VK_NULL_HANDLE &&
            viewportImageView == outputImage.imageView &&
            viewportImageLayout == outputImage.imageLayout) {
            return;
        }

        destroyViewportTexture();
        viewportImageView   = outputImage.imageView;
        viewportImageLayout = outputImage.imageLayout;
        viewportTexture     = ImGuiLayer::addTexture(outputImage.imageView, outputImage.imageLayout);
    }

    void ViewportPanel::destroyViewportTexture() {
        if (viewportTexture != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(projectLayer.getRenderer().device().device());
            ImGuiLayer::removeTexture(viewportTexture);
            viewportTexture = VK_NULL_HANDLE;
        }

        viewportImageView   = VK_NULL_HANDLE;
        viewportImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    }
}
