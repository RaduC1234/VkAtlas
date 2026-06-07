#include <imgui.h>
#define IMVIEWGUIZMO_IMPLEMENTATION
#include <ImViewGuizmo.h>

#include "ViewportPanel.hpp"

#include "core/IconRegistry.hpp"
#include "core/ImGuiLayer.hpp"
#include "ui/components/ToolbarIsland.hpp"
#include "ui/components/ViewportGizmo.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>

#include <ImViewGuizmo.h>

namespace Atlas::Editor {
    float matchedOrthographicHalfHeight(
        entt::registry &registry,
        const entt::entity cameraEntity,
        const entt::entity focusEntity,
        const CameraComponent &cameraComponent) {
        const auto *cameraTransform = registry.try_get<TransformComponent>(cameraEntity);
        if (!cameraTransform) {
            return cameraComponent.orthographicHalfHeight;
        }

        const Camera::Data cameraData = cameraComponent.camera.getData();
        float focusDistance = 0.0f;
        if (focusEntity != entt::null && registry.valid(focusEntity)) {
            if (const auto *focusTransform = registry.try_get<TransformComponent>(focusEntity)) {
                focusDistance = glm::dot(focusTransform->translation - cameraTransform->translation, cameraData.direction);
            }
        }

        if (focusDistance <= 0.001f) {
            focusDistance = glm::dot(-cameraTransform->translation, cameraData.direction);
        }

        if (focusDistance <= 0.001f) {
            focusDistance = 3.0f;
        }

        return std::max(0.001f, focusDistance * std::tan(cameraComponent.perspectiveFovY * 0.5f));
    }

    bool selectedViewGizmoPivot(
        entt::registry &registry,
        const entt::entity selected,
        const entt::entity cameraEntity,
        glm::vec3 &pivot) {
        if (selected == entt::null || selected == cameraEntity || !registry.valid(selected)) {
            return false;
        }

        const auto *transform = registry.try_get<TransformComponent>(selected);
        if (!transform) {
            return false;
        }

        pivot = transform->translation;
        if (const auto *model = registry.try_get<ModelComponent>(selected)) {
            if (const Mesh *mesh = model->meshHandle.get()) {
                pivot = glm::vec3(transform->mat4() * glm::vec4(ViewportGizmo::meshLocalCenter(*mesh), 1.0f));
            }
        }

        return true;
    }

    bool viewportLightUsesDirection(const LightType type) {
        return type == LightType::SPOT || type == LightType::DIRECTIONAL || type == LightType::RECT;
    }

    void viewportSyncLightFromTransform(entt::registry &registry, const entt::entity entity, const TransformComponent &transform) {
        auto *light = registry.try_get<LightComponent>(entity);
        if (!light || !viewportLightUsesDirection(light->type)) {
            return;
        }

        registry.patch<LightComponent>(entity, [&](auto &component) {
            component.direction = ViewportGizmo::lightDirectionFromTransform(transform);
            if (component.type == LightType::RECT) {
                component.rectRight = ViewportGizmo::lightRightFromTransform(transform);
                component.rectUp = ViewportGizmo::lightUpFromTransform(transform);
            }
        });
    }

    bool viewportScreenRay(
        const Camera::Data &cameraData,
        const ImVec2 imageMin,
        const ImVec2 imageSize,
        const ImVec2 screenPosition,
        glm::vec3 &rayOrigin,
        glm::vec3 &rayDirection) {
        if (imageSize.x <= 0.0f || imageSize.y <= 0.0f) {
            return false;
        }

        const float ndcX = ((screenPosition.x - imageMin.x) / imageSize.x) * 2.0f - 1.0f;
        const float ndcY = ((screenPosition.y - imageMin.y) / imageSize.y) * 2.0f - 1.0f;
        const glm::mat4 inverseViewProjection = glm::inverse(cameraData.viewProjection);

        glm::vec4 nearWorld = inverseViewProjection * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
        glm::vec4 farWorld = inverseViewProjection * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
        if (std::abs(nearWorld.w) <= 0.0001f || std::abs(farWorld.w) <= 0.0001f) {
            return false;
        }

        nearWorld /= nearWorld.w;
        farWorld /= farWorld.w;
        rayOrigin = glm::vec3(nearWorld);
        rayDirection = ViewportGizmo::safeDirection(glm::vec3(farWorld - nearWorld), cameraData.direction);
        return true;
    }

    bool viewportScreenPlanePoint(
        const Camera::Data &cameraData,
        const ImVec2 imageMin,
        const ImVec2 imageSize,
        const ImVec2 screenPosition,
        const glm::vec3 &planePoint,
        const glm::vec3 &planeNormal,
        glm::vec3 &worldPoint) {
        glm::vec3 rayOrigin{};
        glm::vec3 rayDirection{};
        if (!viewportScreenRay(cameraData, imageMin, imageSize, screenPosition, rayOrigin, rayDirection)) {
            return false;
        }

        const float denominator = glm::dot(rayDirection, planeNormal);
        if (std::abs(denominator) <= 0.0001f) {
            return false;
        }

        const float distance = glm::dot(planePoint - rayOrigin, planeNormal) / denominator;
        if (distance < 0.0f) {
            return false;
        }

        worldPoint = rayOrigin + rayDirection * distance;
        return true;
    }

    std::vector<entt::entity> viewportSelectedTransformTargets(
        entt::registry &registry,
        const entt::entity activeEntity,
        const std::vector<entt::entity> &selectedEntities) {
        std::vector<entt::entity> targets;
        targets.reserve(selectedEntities.size() + 1);

        for (const entt::entity entity: selectedEntities) {
            if (entity != entt::null &&
                registry.valid(entity) &&
                registry.all_of<TransformComponent>(entity) &&
                !registry.all_of<TransientComponent>(entity) &&
                std::ranges::find(targets, entity) == targets.end()) {
                targets.push_back(entity);
            }
        }

        if (activeEntity != entt::null &&
            registry.valid(activeEntity) &&
            registry.all_of<TransformComponent>(activeEntity) &&
            !registry.all_of<TransientComponent>(activeEntity) &&
            std::ranges::find(targets, activeEntity) == targets.end()) {
            targets.push_back(activeEntity);
        }

        return targets;
    }

    std::vector<ViewportTransformEditSnapshot> viewportTransformSnapshots(
        entt::registry &registry,
        const std::vector<entt::entity> &targets) {
        std::vector<ViewportTransformEditSnapshot> snapshots;
        snapshots.reserve(targets.size());
        for (const entt::entity entity: targets) {
            snapshots.push_back({entity, registry.get<TransformComponent>(entity)});
        }
        return snapshots;
    }

    // ── Construction / destruction ────────────────────────────────────────

    ViewportPanel::ViewportPanel(
        ProjectLayer &projectLayer,
        entt::entity &selectedEntity,
        std::vector<entt::entity> &selectedEntities,
        EditorHistory &history,
        IconRegistry &iconRegistry)
        : projectLayer(projectLayer)
          , selectedEntity(selectedEntity)
          , selectedEntities(selectedEntities)
          , history(history)
          , iconRegistry(iconRegistry) {
    }

    ViewportPanel::~ViewportPanel() {
        destroyViewportTexture();
    }

    void ViewportPanel::onDetach() {
        destroyViewportTexture();
    }

    // ── Main render entry ─────────────────────────────────────────────────

    void ViewportPanel::onImGuiRender() {
        if (!visible) return;

        if (viewportTexture == VK_NULL_HANDLE || !ImViewGuizmo::IsUsing())
            createViewportTexture();

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("Viewport", &visible);
        ImGui::PopStyleVar();

        const ImVec2 size = ImGui::GetContentRegionAvail();
        if (size.x > 1.0f && size.y > 1.0f) {
            projectLayer.getRenderer().setSceneViewportExtent({
                static_cast<uint32_t>(size.x),
                static_cast<uint32_t>(size.y)
            });
        }

        if (viewportTexture != VK_NULL_HANDLE && size.x > 1.0f && size.y > 1.0f) {
            const ImVec2 imageMin = ImGui::GetCursorScreenPos();
            ImGui::Image(viewportTexture, size);
            const bool viewportHovered = ImGui::IsItemHovered();

            if (viewportHovered) {
                const float scrollY = ImGui::GetIO().MouseWheel;
                if (scrollY != 0.0f) {
                    if (auto *scene = projectLayer.project().scene()) {
                        auto &registry = scene->getRegistry();
                        const entt::entity camEntity = activeCamera(registry);
                        if (camEntity != entt::null) {
                            auto *cam = registry.try_get<CameraComponent>(camEntity);
                            if (cam) {
                                if (cam->projection == Camera::Projection::ORTHOGRAPHIC) {
                                    constexpr float zoomSensitivity = 0.1f;
                                    cam->orthographicHalfHeight *= std::pow(1.0f - zoomSensitivity, scrollY);
                                    cam->orthographicHalfHeight = std::max(cam->orthographicHalfHeight, 0.001f);
                                } else {
                                    auto *tf = registry.try_get<TransformComponent>(camEntity);
                                    if (tf) {
                                        const Camera::Data data = cam->camera.getData();
                                        const float speed = scrollY * 0.5f * std::max(glm::length(tf->translation) * 0.1f, 0.1f);
                                        tf->translation += data.direction * speed;
                                        registry.patch<TransformComponent>(camEntity);
                                    }
                                }
                                registry.patch<CameraComponent>(camEntity);
                            }
                        }
                    }
                }
            }

            ImViewGuizmo::BeginFrame();
            renderLightBillboards(imageMin, size);
            renderRectLightControls(imageMin, size);
            renderObjectGizmo(imageMin, size, viewportHovered);
            renderLightDirectionControls(imageMin, size);
            renderViewGizmo(imageMin, size);
            renderContextMenu(viewportHovered && !ImViewGuizmo::IsOver() && !ImViewGuizmo::IsUsing());
            renderToolbar(imageMin, size);
            renderFpsCounter(imageMin, size);
        }

        ImGui::End();
    }

    void ViewportPanel::renderRectLightControls(const ImVec2 imageMin, const ImVec2 imageSize) {
        auto *scene = projectLayer.project().scene();
        if (!scene) return;

        auto &registry = scene->getRegistry();
        if (selectedEntity == entt::null
            || !registry.valid(selectedEntity)
            || !registry.all_of<TransformComponent, LightComponent>(selectedEntity)) {
            return;
        }

        auto &light = registry.get<LightComponent>(selectedEntity);
        if (light.type != LightType::RECT) {
            return;
        }

        const entt::entity cameraEntity = activeCamera(registry);
        if (cameraEntity == entt::null) return;

        const auto cameraData = registry.get<CameraComponent>(cameraEntity).camera.getData();
        const auto &transform = registry.get<TransformComponent>(selectedEntity);

        std::array<ImVec2, 4> corners{};
        ImVec2 center{};
        ImVec2 widthHandle{};
        ImVec2 heightHandle{};
        if (!ViewportGizmo::projectRectLight(cameraData, transform.translation, light, imageMin, imageSize, corners, center, widthHandle, heightHandle)) {
            return;
        }

        const ImVec2 restoreCursor = ImGui::GetCursorScreenPos();
        auto drawHandle = [&](const char *id, const ImVec2 handle, float &value) {
            constexpr float radius = 9.0f;
            ImGui::SetCursorScreenPos({handle.x - radius, handle.y - radius});
            ImGui::PushID(id);
            ImGui::InvisibleButton("##rect_light_handle", {radius * 2.0f, radius * 2.0f});
            const bool hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemActivated() && (!rectLightEditActive || rectLightEditEntity != selectedEntity)) {
                rectLightEditActive = true;
                rectLightEditEntity = selectedEntity;
                rectLightEditBefore = light;
            }
            if (ImGui::IsItemActive()) {
                const ImGuiIO &io = ImGui::GetIO();
                const ImVec2 axis{handle.x - center.x, handle.y - center.y};
                const float axisLength = std::sqrt(axis.x * axis.x + axis.y * axis.y);
                if (axisLength > 0.001f) {
                    const ImVec2 direction{axis.x / axisLength, axis.y / axisLength};
                    const float projectedDelta = io.MouseDelta.x * direction.x + io.MouseDelta.y * direction.y;
                    value = std::max(0.01f, value + projectedDelta * 0.02f);
                }
                registry.patch<LightComponent>(selectedEntity);
            }
            ImGui::PopID();
            return hovered || ImGui::IsItemActive();
        };

        const bool widthHovered = drawHandle("width", widthHandle, light.width);
        const bool heightHovered = drawHandle("height", heightHandle, light.height);
        ViewportGizmo::drawRectLight(*ImGui::GetWindowDrawList(), corners, center, widthHandle, heightHandle, light, widthHovered, heightHovered);

        if (rectLightEditActive && rectLightEditEntity == selectedEntity && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            history.recordLight(selectedEntity, rectLightEditBefore, light);
            rectLightEditActive = false;
            rectLightEditEntity = entt::null;
        }

        ImGui::SetCursorScreenPos(restoreCursor);
        ImGui::Dummy({0, 0});
    }

    void ViewportPanel::renderLightDirectionControls(const ImVec2 imageMin, const ImVec2 imageSize) {
        auto *scene = projectLayer.project().scene();
        if (!scene) return;

        auto &registry = scene->getRegistry();
        if (selectedEntity == entt::null
            || !registry.valid(selectedEntity)
            || !registry.all_of<TransformComponent, LightComponent>(selectedEntity)) {
            if (lightDirectionEditActive && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                lightDirectionEditActive = false;
                lightDirectionEditEntity = entt::null;
            }
            return;
        }

        auto &light = registry.get<LightComponent>(selectedEntity);
        if (light.type != LightType::DIRECTIONAL && light.type != LightType::SPOT) {
            if (lightDirectionEditActive && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                lightDirectionEditActive = false;
                lightDirectionEditEntity = entt::null;
            }
            return;
        }

        const entt::entity cameraEntity = activeCamera(registry);
        if (cameraEntity == entt::null) return;

        const Camera::Data cameraData = registry.get<CameraComponent>(cameraEntity).camera.getData();
        const auto &transform = registry.get<TransformComponent>(selectedEntity);
        const glm::vec3 origin = transform.translation;
        const glm::vec3 direction = ViewportGizmo::safeDirection(light.direction);
        const float cameraDistance = glm::length(origin - cameraData.position);
        const float handleDistance = std::clamp(cameraDistance * 0.25f, 1.0f, 4.0f);
        const glm::vec3 target = origin + direction * handleDistance;

        ImVec2 originScreen{};
        ImVec2 targetScreen{};
        if (!ViewportGizmo::projectPoint(cameraData, origin, imageMin, imageSize, originScreen)
            || !ViewportGizmo::projectPoint(cameraData, target, imageMin, imageSize, targetScreen)) {
            return;
        }

        const ImVec2 restoreCursor = ImGui::GetCursorScreenPos();
        constexpr float hitRadius = 12.0f;
        ImGui::SetCursorScreenPos({targetScreen.x - hitRadius, targetScreen.y - hitRadius});
        ImGui::PushID(static_cast<int>(entt::to_integral(selectedEntity)));
        ImGui::InvisibleButton("##light_direction_target", {hitRadius * 2.0f, hitRadius * 2.0f});
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();

        if (hovered) {
            ImGui::SetTooltip("Drag to aim the light");
        }

        if (ImGui::IsItemActivated() && (!lightDirectionEditActive || lightDirectionEditEntity != selectedEntity)) {
            lightDirectionEditActive = true;
            lightDirectionEditEntity = selectedEntity;
            lightDirectionEditBefore = light;
        }

        if (active) {
            glm::vec3 targetWorld{};
            const ImGuiIO &io = ImGui::GetIO();
            if (viewportScreenPlanePoint(cameraData, imageMin, imageSize, io.MousePos, target, cameraData.direction, targetWorld)) {
                const glm::vec3 nextDirection = targetWorld - origin;
                if (glm::dot(nextDirection, nextDirection) > 0.0001f) {
                    const glm::vec3 normalizedDirection = ViewportGizmo::safeDirection(nextDirection);
                    registry.patch<LightComponent>(selectedEntity, [&](auto &component) {
                        component.direction = normalizedDirection;
                    });
                    registry.patch<TransformComponent>(selectedEntity, [&](auto &component) {
                        component.rotation = ViewportGizmo::transformRotationFromLightDirection(normalizedDirection);
                    });
                }
            }
        }

        ImGui::PopID();

        ImDrawList *drawList = ImGui::GetWindowDrawList();
        const ImU32 lineColor = IM_COL32(255, 218, 120, active ? 255 : 220);
        const ImU32 glowColor = IM_COL32(255, 218, 120, active ? 80 : 45);
        const ImU32 originColor = IM_COL32(255, 255, 255, 220);
        const ImU32 targetColor = hovered || active ? IM_COL32(90, 180, 255, 255) : IM_COL32(255, 218, 120, 255);

        drawList->AddLine(originScreen, targetScreen, glowColor, 7.0f);
        drawList->AddLine(originScreen, targetScreen, lineColor, 2.2f);
        drawList->AddCircleFilled(originScreen, 5.0f, originColor);
        drawList->AddCircle(originScreen, 7.0f, IM_COL32(0, 0, 0, 130), 24, 1.5f);

        const ImVec2 screenAxis{targetScreen.x - originScreen.x, targetScreen.y - originScreen.y};
        const float screenAxisLength = std::sqrt(screenAxis.x * screenAxis.x + screenAxis.y * screenAxis.y);
        if (screenAxisLength > 0.001f) {
            const ImVec2 axis{screenAxis.x / screenAxisLength, screenAxis.y / screenAxisLength};
            const ImVec2 normal{-axis.y, axis.x};
            const ImVec2 arrowBase{targetScreen.x - axis.x * 13.0f, targetScreen.y - axis.y * 13.0f};
            drawList->AddTriangleFilled(
                targetScreen,
                {arrowBase.x + normal.x * 6.0f, arrowBase.y + normal.y * 6.0f},
                {arrowBase.x - normal.x * 6.0f, arrowBase.y - normal.y * 6.0f},
                targetColor);
        }

        drawList->AddCircleFilled(targetScreen, active ? 8.0f : hovered ? 7.0f : 6.0f, targetColor);
        drawList->AddCircle(targetScreen, active ? 11.0f : 9.0f, IM_COL32(0, 0, 0, 150), 24, 1.5f);

        if (lightDirectionEditActive && lightDirectionEditEntity == selectedEntity && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const auto &after = registry.get<LightComponent>(selectedEntity);
            history.recordLight(selectedEntity, lightDirectionEditBefore, after);
            lightDirectionEditActive = false;
            lightDirectionEditEntity = entt::null;
        }

        if (hovered || active) {
            ImGui::SetNextFrameWantCaptureMouse(true);
        }

        ImGui::SetCursorScreenPos(restoreCursor);
        ImGui::Dummy({0, 0});
    }

    // ── Toolbar ───────────────────────────────────────────────────────────

    void ViewportPanel::renderToolbar(const ImVec2 imageMin, const ImVec2 imageSize) {
        const ImVec2 restoreCursor = ImGui::GetCursorScreenPos();

        entt::registry *registry = nullptr;
        entt::entity cameraEntity = entt::null;
        CameraComponent *cameraComponent = nullptr;
        if (auto *scene = projectLayer.project().scene()) {
            registry = &scene->getRegistry();
            cameraEntity = activeCamera(*registry);
            if (cameraEntity != entt::null) {
                cameraComponent = registry->try_get<CameraComponent>(cameraEntity);
            }
        }

        // ── Islands ───────────────────────────────────────────────────────
        // Each island is a self-contained component: declare, chain props, render.

        ToolbarIsland transformIsland(imageMin, imageSize);
        transformIsland
                .anchor(ToolbarIsland::Anchor::TopLeft)
                .buttons(3)
                .render([&](float &x, float y) {
                    if (IconButton("##t", "translate", iconRegistry).active(objectGizmoMode == ObjectGizmoMode::Translate).tooltip("Translate  W").render(x, y))
                        objectGizmoMode = ObjectGizmoMode::Translate;
                    if (IconButton("##r", "rotate", iconRegistry).active(objectGizmoMode == ObjectGizmoMode::Rotate).tooltip("Rotate  E").render(x, y))
                        objectGizmoMode = ObjectGizmoMode::Rotate;
                    if (IconButton("##s", "scale", iconRegistry).active(objectGizmoMode == ObjectGizmoMode::Scale).tooltip("Scale  R").render(x, y))
                        objectGizmoMode = ObjectGizmoMode::Scale;
                });

        // Space island sits to the right of the transform island
        const ImVec2 spaceAnchor(transformIsland.max().x + ToolbarStyle::defaults().islandMargin,imageMin.y);
        ToolbarIsland spaceIsland(spaceAnchor, imageSize);
        spaceIsland
                .anchor(ToolbarIsland::Anchor::TopLeft, {0, ToolbarStyle::defaults().islandMargin})
                .buttons(2)
                .render([&](float &x, float y) {
                    if (IconButton("##local", "local", iconRegistry).active(objectGizmoSpace == ObjectGizmoSpace::Local).tooltip("Local space").render(x, y))
                        objectGizmoSpace = ObjectGizmoSpace::Local;
                    if (IconButton("##world", "world", iconRegistry).active(objectGizmoSpace == ObjectGizmoSpace::World).tooltip("World space").render(x, y))
                        objectGizmoSpace = ObjectGizmoSpace::World;
                });

        // Gizmo settings popup — small "⚙" button right of the space island
        {
            const ToolbarStyle ts = ToolbarStyle::defaults();
            const float margin = ts.islandMargin;
            const ImVec2 btnMin{spaceIsland.max().x + margin, imageMin.y + margin};
            const ImVec2 btnMax{btnMin.x + ts.btnW, btnMin.y + ts.btnH};

            ImGui::SetCursorScreenPos(btnMin);
            ImGui::InvisibleButton("##gizmo_settings_btn", {ts.btnW, ts.btnH});
            const bool settingsHovered = ImGui::IsItemHovered();
            const bool settingsClicked = ImGui::IsItemClicked();
            if (settingsClicked)
                ImGui::OpenPopup("##gizmo_settings_popup");

            ImDrawList *dl = ImGui::GetWindowDrawList();
            if (settingsHovered)
                dl->AddRectFilled(btnMin, btnMax, ts.colHover, 7.0f);
            dl->AddRectFilled(btnMin, btnMax, settingsHovered ? ts.colHover : IM_COL32(30, 32, 38, 180), 7.0f);

            const char *icon = "\xe2\x9a\x99";
            const ImVec2 iconSize = ImGui::CalcTextSize(icon);
            dl->AddText({btnMin.x + (ts.btnW - iconSize.x) * 0.5f, btnMin.y + (ts.btnH - iconSize.y) * 0.5f},
                        IM_COL32(210, 215, 225, 220), icon);

            ImGui::SetNextWindowPos({btnMin.x, btnMax.y + 4.0f});
            ImGui::SetNextWindowSize({220.0f, 0.0f});
            if (ImGui::BeginPopup("##gizmo_settings_popup")) {
                auto &gs = ImViewGuizmo::GetStyle();

                ImGui::TextDisabled("Transform Gizmo");
                ImGui::Separator();

                ImGui::SetNextItemWidth(-1);
                ImGui::DragFloat("##move_scale",  &gs.transformMoveScale,  0.01f, 0.01f, 100.0f, "Move Scale: %.2f");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Multiplier on translate drag distance");

                ImGui::SetNextItemWidth(-1);
                ImGui::DragFloat("##scale_speed", &gs.transformScaleSpeed, 0.001f, 0.001f, 1.0f, "Scale Speed: %.3f");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Multiplier on scale drag distance");

                ImGui::SetNextItemWidth(-1);
                ImGui::DragFloat("##fade_px", &gs.transformAxisFadePixels, 0.5f, 0.0f, 40.0f, "Axis Fade: %.1f px");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hide axis when its screen projection is shorter than this");

                ImGui::Checkbox("Show Delta Label", &gs.transformShowDelta);

                ImGui::Spacing();
                if (ImGui::SmallButton("Reset")) {
                    gs.transformMoveScale     = 1.0f;
                    gs.transformScaleSpeed    = 0.01f;
                    gs.transformAxisFadePixels = 6.0f;
                    gs.transformShowDelta     = true;
                }

                ImGui::EndPopup();
            }
        }

        const ImVec2 projectionAnchor(spaceIsland.max().x + ToolbarStyle::defaults().islandMargin * 2.0f + ToolbarStyle::defaults().btnW + ToolbarStyle::defaults().islandMargin, imageMin.y);
        ToolbarIsland projectionIsland(projectionAnchor, imageSize);
        projectionIsland
                .anchor(ToolbarIsland::Anchor::TopLeft, {0, ToolbarStyle::defaults().islandMargin})
                .buttons(1)
                .render([&](float &x, float y) {
                    const ToolbarStyle style = ToolbarStyle::defaults();
                    const ImVec2 buttonMin{x, y};
                    const ImVec2 buttonMax{x + style.btnW, y + style.btnH};
                    ImGui::SetCursorScreenPos(buttonMin);
                    ImGui::InvisibleButton("##camera_projection", {style.btnW, style.btnH});
                    const bool hovered = ImGui::IsItemHovered();
                    const bool clicked = ImGui::IsItemClicked();

                    if (hovered) {
                        ImGui::SetTooltip("%s", cameraComponent && cameraComponent->projection == Camera::Projection::ORTHOGRAPHIC
                                                   ? "Orthographic camera"
                                                   : "Perspective camera");
                    }

                    if (clicked && cameraComponent && registry && cameraEntity != entt::null) {
                        if (cameraComponent->projection == Camera::Projection::ORTHOGRAPHIC) {
                            cameraComponent->projection = Camera::Projection::PERSPECTIVE;
                        } else {
                            cameraComponent->orthographicHalfHeight = matchedOrthographicHalfHeight(
                                *registry,
                                cameraEntity,
                                selectedEntity,
                                *cameraComponent);
                            cameraComponent->projection = Camera::Projection::ORTHOGRAPHIC;
                        }
                        registry->patch<CameraComponent>(cameraEntity);
                    }

                    ImDrawList *drawList = ImGui::GetWindowDrawList();
                    if (hovered) {
                        drawList->AddRectFilled(buttonMin, buttonMax, style.colHover, 7.0f);
                    }

                    const char *label = cameraComponent && cameraComponent->projection == Camera::Projection::ORTHOGRAPHIC ? "ORT" : "PER";
                    const ImVec2 textSize = ImGui::CalcTextSize(label);
                    drawList->AddText(
                        {buttonMin.x + (style.btnW - textSize.x) * 0.5f, buttonMin.y + (style.btnH - textSize.y) * 0.5f},
                        IM_COL32(232, 238, 246, 255),
                        label);
                    drawList->AddLine(
                        {buttonMin.x + 8.0f, buttonMax.y - 3.0f},
                        {buttonMax.x - 8.0f, buttonMax.y - 3.0f},
                        style.colAccent,
                        2.0f);

                    x += style.btnW;
                });

        // View mode island — anchored to the top-right of the viewport
        struct VMEntry {
            const char *icon;
            const char *tip;
            ViewMode val;
        };
        constexpr VMEntry vms[] = {
            {"viewmode_lit", "Lit", ViewMode::LIT},
            {"viewmode_unlit", "Unlit", ViewMode::UNLIT},
            {"viewmode_clay", "Clay", ViewMode::LIGHTING_ONLY},
            {"viewmode_path_traced", "Path Traced", ViewMode::PATH_TRACING},
        };

        ToolbarIsland viewModeIsland(imageMin, imageSize);
        viewModeIsland
                .anchor(ToolbarIsland::Anchor::TopRight)
                .buttons(static_cast<int>(std::size(vms)))
                .render([&](float &x, float y) {
                    for (int i = 0; i < static_cast<int>(std::size(vms)); ++i) {
                        char id[16];
                        std::snprintf(id, sizeof(id), "##vm%d", i);
                        const bool active = cameraComponent && cameraComponent->renderMode == vms[i].val;
                        if (IconButton(id, vms[i].icon, iconRegistry).active(active).tooltip(vms[i].tip).render(x, y) && cameraComponent && registry) {
                            cameraComponent->renderMode = vms[i].val;
                            registry->patch<CameraComponent>(cameraEntity);
                        }
                    }
                });

        ImGui::SetCursorScreenPos(restoreCursor);
        ImGui::Dummy({0, 0});
    }

    void ViewportPanel::renderFpsCounter(const ImVec2 imageMin, const ImVec2 imageSize) {
        const ImGuiIO &io = ImGui::GetIO();
        const float frameTimeMs = io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f;

        char text[64]{};
        std::snprintf(text, sizeof(text), "%.1f FPS  %.2f ms", io.Framerate, frameTimeMs);

        ImDrawList *drawList = ImGui::GetWindowDrawList();
        const ImVec2 textSize = ImGui::CalcTextSize(text);
        const ImVec2 padding{10.0f, 6.0f};
        const ImVec2 margin{12.0f, 12.0f};
        const ImVec2 min{
            imageMin.x + margin.x,
            imageMin.y + imageSize.y - textSize.y - padding.y * 2.0f - margin.y
        };
        const ImVec2 max{
            min.x + textSize.x + padding.x * 2.0f,
            min.y + textSize.y + padding.y * 2.0f
        };

        drawList->AddRectFilled(min, max, IM_COL32(8, 10, 14, 190), 7.0f);
        drawList->AddRect(min, max, IM_COL32(255, 255, 255, 38), 7.0f);
        drawList->AddText({min.x + padding.x, min.y + padding.y}, IM_COL32(232, 238, 246, 255), text);
    }

    // ── Context menu ──────────────────────────────────────────────────────

    void ViewportPanel::renderContextMenu(const bool viewportHovered) {
        if (viewportHovered && ImGui::Shortcut(ImGuiMod_Shift | ImGuiKey_A))
            ImGui::OpenPopup("##viewport_context_menu");

        if (!ImGui::BeginPopup("##viewport_context_menu")) return;

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

    // ── Light billboards ──────────────────────────────────────────────────

    void ViewportPanel::renderLightBillboards(const ImVec2 imageMin, const ImVec2 imageSize) {
        auto *scene = projectLayer.project().scene();
        if (!scene) return;

        auto &registry = scene->getRegistry();
        const entt::entity cameraEntity = activeCamera(registry);
        if (cameraEntity == entt::null) return;

        const auto cameraData = registry.get<CameraComponent>(cameraEntity).camera.getData();
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const ImVec2 restoreCursor = ImGui::GetCursorScreenPos();

        for (const entt::entity entity: registry.view<TransformComponent, LightComponent>()) {
            if (const auto *node = registry.try_get<SceneNodeComponent>(entity);
                node && (node->deleted || !node->visible))
                continue;

            const auto &transform = registry.get<TransformComponent>(entity);
            const auto &light = registry.get<LightComponent>(entity);

            std::array<ImVec2, 4> corners{};
            ImVec2 screenPos{};
            const float size = (selectedEntity == entity) ? 0.42f : 0.32f;

            if (!ViewportGizmo::projectLightBillboard(
                cameraData, transform.translation, imageMin, imageSize, size, corners, screenPos))
                continue;

            // Hit rect
            ImVec2 hitMin = corners[0], hitMax = corners[0];
            for (const ImVec2 &c: corners) {
                hitMin.x = std::min(hitMin.x, c.x);
                hitMin.y = std::min(hitMin.y, c.y);
                hitMax.x = std::max(hitMax.x, c.x);
                hitMax.y = std::max(hitMax.y, c.y);
            }
            constexpr float minHit = 14.0f;
            auto expand = [](float &lo, float &hi, float min) {
                if (hi - lo < min) {
                    float e = (min - (hi - lo)) * 0.5f;
                    lo -= e;
                    hi += e;
                }
            };
            expand(hitMin.x, hitMax.x, minHit);
            expand(hitMin.y, hitMax.y, minHit);

            ImGui::SetCursorScreenPos(hitMin);
            ImGui::PushID(static_cast<int>(entt::to_integral(entity)));
            const bool clicked = ImGui::InvisibleButton("##billboard", {hitMax.x - hitMin.x, hitMax.y - hitMin.y});
            const bool hovered = ImGui::IsItemHovered();
            ImGui::PopID();

            if (clicked) {
                selectedEntity = entity;
                selectedEntities = {entity};
            }

            ViewportGizmo::drawLightBillboard(*dl, corners, screenPos, light, selectedEntity == entity, iconRegistry);

            if (hovered) {
                const auto *node = registry.try_get<SceneNodeComponent>(entity);
                ImGui::SetTooltip("%s", (node && !node->name.empty())
                                            ? node->name.c_str()
                                            : ViewportGizmo::lightTypeName(light.type));
            }
        }

        ImGui::SetCursorScreenPos(restoreCursor);
        ImGui::Dummy({0, 0});
    }

    // ── Object gizmo ─────────────────────────────────────────────────────

    void ViewportPanel::renderObjectGizmo(const ImVec2 imageMin, const ImVec2 imageSize, const bool) {
        auto *scene = projectLayer.project().scene();
        if (!scene) return;

        auto &registry = scene->getRegistry();
        if (selectedEntity == entt::null
            || !registry.valid(selectedEntity)
            || !registry.all_of<TransformComponent>(selectedEntity))
            return;

        const entt::entity cameraEntity = activeCamera(registry);
        if (cameraEntity == entt::null) return;

        const auto cameraData = registry.get<CameraComponent>(cameraEntity).camera.getData();
        auto &transform = registry.get<TransformComponent>(selectedEntity);
        const std::vector<entt::entity> transformTargets = viewportSelectedTransformTargets(registry, selectedEntity, selectedEntities);
        if (transformTargets.empty()) {
            return;
        }

        glm::vec3 gizmoPos = transform.translation;
        if (const auto *model = registry.try_get<ModelComponent>(selectedEntity)) {
            if (const Mesh *mesh = model->meshHandle.get())
                gizmoPos = glm::vec3(transform.mat4() * glm::vec4(ViewportGizmo::meshLocalCenter(*mesh), 1.0f));
        }

        glm::vec3 editPos = gizmoPos;
        glm::quat editRot = ViewportGizmo::rotationYXZ(transform.rotation);
        glm::vec3 editScale = transform.scale;
        const glm::vec3 prevPos = editPos;
        const glm::quat prevRot = editRot;
        const glm::vec3 prevScale = editScale;

        const glm::quat gizmoOrientation = (objectGizmoSpace == ObjectGizmoSpace::Local)
                                               ? editRot
                                               : glm::quat{1, 0, 0, 0};

        auto &gizmoStyle = ImViewGuizmo::GetStyle();
        gizmoStyle.scale = 1.0f;
        gizmoStyle.transformAxisLength = 54.0f;
        gizmoStyle.transformRotateRadius = 48.0f;

        const bool modified = ImViewGuizmo::Transform(
            cameraData.viewProjection, imageMin, imageSize,
            ViewportGizmo::toImViewOperation(objectGizmoMode),
            editPos, editRot, editScale, gizmoOrientation);

        if (modified) {
            if (!objectTransformEditActive || objectTransformEditEntity != selectedEntity) {
                objectTransformEditActive = true;
                objectTransformEditEntity = selectedEntity;
                objectTransformEditBefore = viewportTransformSnapshots(registry, transformTargets);
            }

            const glm::vec3 translationDelta = editPos - prevPos;
            const glm::quat rotationDelta = editRot * glm::inverse(prevRot);
            const glm::vec3 scaleRatio{
                std::abs(prevScale.x) > 0.0001f ? editScale.x / prevScale.x : 1.0f,
                std::abs(prevScale.y) > 0.0001f ? editScale.y / prevScale.y : 1.0f,
                std::abs(prevScale.z) > 0.0001f ? editScale.z / prevScale.z : 1.0f,
            };

            for (const entt::entity entity: transformTargets) {
                auto &targetTransform = registry.get<TransformComponent>(entity);

                if (objectGizmoMode == ObjectGizmoMode::Translate) {
                    targetTransform.translation += translationDelta;
                } else if (objectGizmoMode == ObjectGizmoMode::Rotate) {
                    if (entity == selectedEntity) {
                        targetTransform.rotation = ViewportGizmo::eulerFromRotationYXZ(editRot);
                    } else {
                        targetTransform.translation = gizmoPos + rotationDelta * (targetTransform.translation - gizmoPos);
                        targetTransform.rotation = ViewportGizmo::eulerFromRotationYXZ(rotationDelta * ViewportGizmo::rotationYXZ(targetTransform.rotation));
                    }
                } else if (objectGizmoMode == ObjectGizmoMode::Scale) {
                    if (entity == selectedEntity) {
                        targetTransform.scale = editScale;
                    } else {
                        const glm::vec3 offset = targetTransform.translation - gizmoPos;
                        targetTransform.translation = gizmoPos + offset * scaleRatio;
                        targetTransform.scale *= scaleRatio;
                    }
                }

                registry.patch<TransformComponent>(entity);
                if (objectGizmoMode == ObjectGizmoMode::Rotate) {
                    viewportSyncLightFromTransform(registry, entity, targetTransform);
                }

                if (registry.all_of<CameraComponent>(entity)) {
                    registry.patch<CameraComponent>(entity, [&](auto &cc) {
                        cc.camera.setViewYXZ(targetTransform.translation, targetTransform.rotation);
                    });
                }
            }
        }

        if (objectTransformEditActive
            && objectTransformEditEntity == selectedEntity
            && !ImViewGuizmo::IsTransformUsing()) {
            for (const auto &snapshot: objectTransformEditBefore) {
                if (snapshot.entity != entt::null && registry.valid(snapshot.entity) && registry.all_of<TransformComponent>(snapshot.entity)) {
                    history.recordTransform(snapshot.entity, snapshot.before, registry.get<TransformComponent>(snapshot.entity));
                }
            }
            objectTransformEditBefore.clear();
            objectTransformEditActive = false;
            objectTransformEditEntity = entt::null;
        }
    }

    // ── View gizmo ────────────────────────────────────────────────────────

    void ViewportPanel::renderViewGizmo(const ImVec2 imageMin, const ImVec2 imageSize) {
        auto *scene = projectLayer.project().scene();
        if (!scene || imageSize.x < 160.0f || imageSize.y < 160.0f) return;

        auto &registry = scene->getRegistry();
        const entt::entity cameraEntity = activeCamera(registry);
        if (cameraEntity == entt::null) return;

        const auto &transform = registry.get<TransformComponent>(cameraEntity);
        const float pitch = transform.rotation.x;
        const float yaw = transform.rotation.y;

        glm::vec3 forward = glm::normalize(glm::vec3{
            std::cos(pitch) * std::sin(yaw),
            -std::sin(pitch),
            std::cos(pitch) * std::cos(yaw)
        });

        glm::vec3 worldUp = (std::abs(forward.y) > 0.99f)
                                ? glm::vec3{1, 0, 0}
                                : glm::vec3{0, 1, 0};

        const glm::vec3 right = glm::normalize(glm::cross(worldUp, forward));
        const glm::vec3 up = glm::cross(right, forward);

        if (!ImViewGuizmo::IsUsing())
            cameraRotation = glm::quatLookAt(forward, up);

        constexpr float gizmoScale = 0.64f;
        constexpr float paddingX = 14.0f;
        constexpr float paddingY = 34.0f;
        constexpr float halfExtent = 128.0f * gizmoScale;

        auto &gs = ImViewGuizmo::GetStyle();
        gs.scale = gizmoScale;
        gs.lineWidth = 4.0f;
        gs.circleRadius = 18.0f;
        gs.highlightWidth = 3.0f;
        gs.bigCircleRadius = 94.0f;
        gs.labelSize = 1.1f;

        const ImVec2 gizmoPos{
            imageMin.x + imageSize.x - halfExtent - paddingX,
            imageMin.y + halfExtent + paddingY
        };

        glm::vec3 pivot = transform.translation;
        const bool hasSelectionPivot = selectedViewGizmoPivot(registry, selectedEntity, cameraEntity, pivot);

        glm::vec3 camPos = transform.translation;
        if (hasSelectionPivot) {
            const glm::vec3 cameraOffset = camPos - pivot;
            if (glm::dot(cameraOffset, cameraOffset) < 0.0001f) {
                camPos = pivot - forward * 3.0f;
            }
        }

        if (ImViewGuizmo::Rotate(camPos, cameraRotation, pivot, gizmoPos, 0.008f)) {
            const glm::vec3 newForward = glm::normalize(cameraRotation * ImViewGuizmo::worldForward);
            registry.patch<TransformComponent>(cameraEntity, [&](auto &tc) {
                tc.translation = camPos;
                tc.rotation.x = std::asin(glm::clamp(-newForward.y, -1.0f, 1.0f));
                tc.rotation.y = glm::mod(std::atan2(newForward.x, newForward.z), glm::two_pi<float>());
                tc.rotation.z = 0.0f;
            });
            registry.patch<CameraComponent>(cameraEntity, [&](auto &cc) {
                const auto &tc = registry.get<TransformComponent>(cameraEntity);
                cc.camera.setViewYXZ(tc.translation, tc.rotation);
            });
        }

        if (ImViewGuizmo::IsOver() || ImViewGuizmo::IsUsing())
            ImGui::SetNextFrameWantCaptureMouse(true);
    }

    // ── Scene helpers ─────────────────────────────────────────────────────

    void ViewportPanel::addPrimitive(const ViewportPrimitive primitive) {
        auto *scene = projectLayer.project().scene();
        if (!scene) return;

        auto &registry = scene->getRegistry();
        const entt::entity entity = registry.create();

        SceneNodeComponent node{};
        node.name = primitiveName(primitive);
        const std::string objectName = node.name;
        registry.emplace<SceneNodeComponent>(entity, std::move(node));

        TransformComponent transform{};
        transform.translation = primitiveSpawnPosition();
        registry.emplace<TransformComponent>(entity, transform);

        ModelComponent model{};
        model.meshHandle = primitiveMesh(primitive);
        registry.emplace<ModelComponent>(entity, model);

        MaterialComponent material{};
        auto mat = std::make_shared<Material>();
        mat->name = objectName + " Material";
        mat->baseColor = glm::vec4{0.82f, 0.82f, 0.78f, 1.0f};
        mat->baseColorTexture = primitiveWhiteTexture();
        material.materialHandle = projectLayer.assetManager().store<Material>(
            std::move(mat), "##editor/materials/" + mat->name);
        registry.emplace<MaterialComponent>(entity, material);

        selectedEntity = entity;
        selectedEntities = {entity};
    }

    void ViewportPanel::addLight(const LightType type) {
        auto *scene = projectLayer.project().scene();
        if (!scene) return;

        auto &registry = scene->getRegistry();
        const entt::entity entity = registry.create();

        SceneNodeComponent node{};
        node.name = lightName(type);
        registry.emplace<SceneNodeComponent>(entity, std::move(node));

        LightComponent light{};
        light.type = type;

        TransformComponent transform{};
        transform.translation = primitiveSpawnPosition();

        if (viewportLightUsesDirection(type)) {
            const entt::entity cameraEntity = activeCamera(registry);
            if (cameraEntity != entt::null) {
                light.direction = ViewportGizmo::safeDirection(
                    registry.get<CameraComponent>(cameraEntity).camera.getData().direction);
            }
            transform.rotation = ViewportGizmo::transformRotationFromLightDirection(light.direction);
        }

        if (type == LightType::RECT) {
            light.width = 2.0f;
            light.height = 1.0f;
            light.rectRight = ViewportGizmo::lightRightFromTransform(transform);
            light.rectUp = ViewportGizmo::lightUpFromTransform(transform);
        }

        registry.emplace<TransformComponent>(entity, transform);
        registry.emplace<LightComponent>(entity, light);

        selectedEntity = entity;
        selectedEntities = {entity};
    }

    AssetHandle<Mesh> ViewportPanel::primitiveMesh(const ViewportPrimitive primitive) {
        const auto makeVertex = [](glm::vec3 pos, glm::vec3 norm, glm::vec2 uv, glm::vec4 tan) {
            Mesh::Vertex v{};
            v.position = pos;
            v.color = glm::vec3{1};
            v.normal = norm;
            v.uv = uv;
            v.tangent = tan;
            return v;
        };

        std::vector<Mesh::Vertex> verts;
        std::vector<uint32_t> inds;

        if (primitive == ViewportPrimitive::Cube) {
            verts.reserve(24);
            inds.reserve(36);

            auto addFace = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d,
                               glm::vec3 n, glm::vec4 t) {
                const uint32_t base = static_cast<uint32_t>(verts.size());
                verts.push_back(makeVertex(a, n, {0, 0}, t));
                verts.push_back(makeVertex(b, n, {1, 0}, t));
                verts.push_back(makeVertex(c, n, {1, 1}, t));
                verts.push_back(makeVertex(d, n, {0, 1}, t));
                inds.insert(inds.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
            };

            constexpr float h = 0.5f;
            addFace({-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}, {0, 0, 1}, {1, 0, 0, 1});
            addFace({h, -h, -h}, {-h, -h, -h}, {-h, h, -h}, {h, h, -h}, {0, 0, -1}, {-1, 0, 0, 1});
            addFace({h, -h, h}, {h, -h, -h}, {h, h, -h}, {h, h, h}, {1, 0, 0}, {0, 0, -1, 1});
            addFace({-h, -h, -h}, {-h, -h, h}, {-h, h, h}, {-h, h, -h}, {-1, 0, 0}, {0, 0, 1, 1});
            addFace({-h, h, h}, {h, h, h}, {h, h, -h}, {-h, h, -h}, {0, 1, 0}, {1, 0, 0, 1});
            addFace({-h, -h, -h}, {h, -h, -h}, {h, -h, h}, {-h, -h, h}, {0, -1, 0}, {1, 0, 0, 1});

            return projectLayer.assetManager().store<Mesh>(
                std::make_shared<Mesh>(verts, inds), "##editor/primitives/cube");
        }

        if (primitive == ViewportPrimitive::Sphere) {
            constexpr uint32_t S = 32, R = 16;
            constexpr float radius = 0.5f;
            verts.reserve((S + 1) * (R + 1));
            inds.reserve(S * R * 6);

            for (uint32_t r = 0; r <= R; ++r) {
                const float v = static_cast<float>(r) / R;
                const float theta = v * glm::pi<float>();
                const float st = std::sin(theta), ct = std::cos(theta);
                for (uint32_t s = 0; s <= S; ++s) {
                    const float u = static_cast<float>(s) / S;
                    const float phi = u * glm::two_pi<float>();
                    const float sp = std::sin(phi), cp = std::cos(phi);
                    const glm::vec3 n{st * cp, ct, st * sp};
                    verts.push_back(makeVertex(n * radius, n, {u, v}, {-sp, 0, cp, 1}));
                }
            }
            for (uint32_t r = 0; r < R; ++r)
                for (uint32_t s = 0; s < S; ++s) {
                    uint32_t a = r * (S + 1) + s, b = (r + 1) * (S + 1) + s, c = b + 1, d = a + 1;
                    inds.insert(inds.end(), {a, d, c, a, c, b});
                }

            return projectLayer.assetManager().store<Mesh>(
                std::make_shared<Mesh>(verts, inds), "##editor/primitives/sphere");
        }

        // Square (two-sided)
        const glm::vec3 tn{0, 1, 0}, bn{0, -1, 0};
        const glm::vec4 tt{1, 0, 0, 1};
        verts = {
            makeVertex({-0.5f, 0, 0.5f}, tn, {0, 0}, tt), makeVertex({0.5f, 0, 0.5f}, tn, {1, 0}, tt),
            makeVertex({0.5f, 0, -0.5f}, tn, {1, 1}, tt), makeVertex({-0.5f, 0, -0.5f}, tn, {0, 1}, tt),
            makeVertex({-0.5f, 0, 0.5f}, bn, {0, 0}, tt), makeVertex({-0.5f, 0, -0.5f}, bn, {0, 1}, tt),
            makeVertex({0.5f, 0, -0.5f}, bn, {1, 1}, tt), makeVertex({0.5f, 0, 0.5f}, bn, {1, 0}, tt),
        };
        inds = {0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};

        return projectLayer.assetManager().store<Mesh>(
            std::make_shared<Mesh>(verts, inds), "##editor/primitives/square_twosided");
    }

    AssetHandle<Texture> ViewportPanel::primitiveWhiteTexture() {
        return projectLayer.assetManager().store<Texture>(Texture::default_(), "##editor/primitives/white");
    }

    entt::entity ViewportPanel::activeCamera(entt::registry &registry) const {
        // Prefer scene cameras (non-transient)
        for (const entt::entity e: registry.view<TransformComponent, CameraComponent>()) {
            if (registry.all_of<TransientComponent>(e)) continue;
            if (const auto *n = registry.try_get<SceneNodeComponent>(e); n && (n->deleted || !n->visible)) continue;
            return e;
        }
        // Fall back to editor camera
        for (const entt::entity e: registry.view<TransformComponent, CameraComponent, EditorCameraComponent>()) {
            if (const auto *n = registry.try_get<SceneNodeComponent>(e); n && (n->deleted || !n->visible)) continue;
            return e;
        }
        return entt::null;
    }

    glm::vec3 ViewportPanel::primitiveSpawnPosition() {
        auto *scene = projectLayer.project().scene();
        if (!scene) return {};

        auto &registry = scene->getRegistry();
        const entt::entity cam = activeCamera(registry);
        if (cam == entt::null) return {};

        const auto &t = registry.get<TransformComponent>(cam);
        const glm::vec3 fwd = glm::normalize(glm::vec3{
            std::cos(t.rotation.x) * std::sin(t.rotation.y),
            -std::sin(t.rotation.x),
            std::cos(t.rotation.x) * std::cos(t.rotation.y)
        });
        return t.translation + fwd * 3.0f;
    }

    static std::string uniqueName(entt::registry &registry, const char *base) {
        auto exists = [&](const std::string &name) {
            for (const entt::entity e: registry.view<SceneNodeComponent>()) {
                if (registry.all_of<TransientComponent>(e)) continue;
                const auto &n = registry.get<SceneNodeComponent>(e);
                if (!n.deleted && n.name == name) return true;
            }
            return false;
        };

        if (!exists(base)) return base;
        char buf[8]{};
        for (uint32_t i = 1; i < 1000; ++i) {
            std::snprintf(buf, sizeof(buf), ".%03u", i);
            std::string candidate = std::string(base) + buf;
            if (!exists(candidate)) return candidate;
        }
        return std::string(base) + ".999";
    }

    std::string ViewportPanel::primitiveName(const ViewportPrimitive primitive) const {
        auto *scene = projectLayer.project().scene();
        const char *base = primitive == ViewportPrimitive::Cube
                               ? "Cube"
                               : primitive == ViewportPrimitive::Square
                                     ? "Square"
                                     : "Sphere";
        if (!scene) {
            return base;
        }

        return uniqueName(scene->getRegistry(), base);
    }

    std::string ViewportPanel::lightName(const LightType type) {
        auto *scene = projectLayer.project().scene();
        const char *base = ViewportGizmo::lightTypeName(type);
        if (!scene) return base;
        return uniqueName(scene->getRegistry(), base);
    }

    void ViewportPanel::createViewportTexture() {
        const auto &img = projectLayer.getRenderer().getSceneOutputImage();
        if (!img.valid()) {
            destroyViewportTexture();
            return;
        }

        if (viewportTexture != VK_NULL_HANDLE && viewportImageView == img.imageView && viewportImageLayout == img.imageLayout) {
            return;
        }

        destroyViewportTexture();
        viewportImageView = img.imageView;
        viewportImageLayout = img.imageLayout;
        viewportTexture = ImGuiLayer::addTexture(img.imageView, img.imageLayout);
        if (viewportTexture == VK_NULL_HANDLE) {
            viewportImageView = VK_NULL_HANDLE;
            viewportImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        }
    }

    void ViewportPanel::destroyViewportTexture() {
        if (viewportTexture == VK_NULL_HANDLE) {
            return;
        }

        vkDeviceWaitIdle(projectLayer.getRenderer().device().device());
        ImGuiLayer::removeTexture(viewportTexture);
        viewportTexture = VK_NULL_HANDLE;
        viewportImageView = VK_NULL_HANDLE;
        viewportImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    }
} // namespace Atlas::Editor
