#include "ViewportPanel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

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
            renderObjectGizmo(imageMin, size, viewportHovered);
            renderViewGizmo(imageMin, size);
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
