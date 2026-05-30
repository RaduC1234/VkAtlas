#include <imgui.h>
#define IMVIEWGUIZMO_IMPLEMENTATION
#include <ImViewGuizmo.h>

#include "ViewportPanel.hpp"

#include "core/IconRegistry.hpp"
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
#include <glm/gtc/quaternion.hpp>

#include <ImViewGuizmo.h>

namespace Atlas::Editor {
    // ── Construction / destruction ────────────────────────────────────────

    ViewportPanel::ViewportPanel(
        ProjectLayer &projectLayer,
        entt::entity &selectedEntity,
        EditorHistory &history,
        IconRegistry &iconRegistry)
        : projectLayer(projectLayer)
          , selectedEntity(selectedEntity)
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

            ImViewGuizmo::BeginFrame();
            renderLightBillboards(imageMin, size);
            renderObjectGizmo(imageMin, size, viewportHovered);
            renderViewGizmo(imageMin, size);
            renderContextMenu(viewportHovered && !ImViewGuizmo::IsOver() && !ImViewGuizmo::IsUsing());
            renderToolbar(imageMin, size);
        }

        ImGui::End();
    }

    // ── Toolbar ───────────────────────────────────────────────────────────

    void ViewportPanel::renderToolbar(const ImVec2 imageMin, const ImVec2 imageSize) {
        const ImVec2 restoreCursor = ImGui::GetCursorScreenPos();

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

        // View mode island — anchored to the top-right of the viewport
        auto &settings = projectLayer.getRenderer().settings();
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
                        if (IconButton(id, vms[i].icon, iconRegistry).active(settings.viewMode == vms[i].val).tooltip(vms[i].tip).render(x, y))
                            settings.viewMode = vms[i].val;
                    }
                });

        ImGui::SetCursorScreenPos(restoreCursor);
        ImGui::Dummy({0, 0});
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

            if (clicked) selectedEntity = entity;

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
        const TransformComponent before = transform;

        glm::vec3 gizmoPos = transform.translation;
        if (const auto *model = registry.try_get<ModelComponent>(selectedEntity)) {
            if (const Mesh *mesh = model->meshHandle.get())
                gizmoPos = glm::vec3(transform.mat4() * glm::vec4(ViewportGizmo::meshLocalCenter(*mesh), 1.0f));
        }

        glm::vec3 editPos = gizmoPos;
        glm::quat editRot = ViewportGizmo::rotationYXZ(transform.rotation);
        glm::vec3 editScale = transform.scale;
        const glm::vec3 prevPos = editPos;

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
                objectTransformEditBefore = before;
            }

            if (objectGizmoMode == ObjectGizmoMode::Translate) transform.translation += editPos - prevPos;
            else if (objectGizmoMode == ObjectGizmoMode::Rotate) transform.rotation = ViewportGizmo::eulerFromRotationYXZ(editRot);
            else if (objectGizmoMode == ObjectGizmoMode::Scale) transform.scale = editScale;

            registry.patch<TransformComponent>(selectedEntity);

            if (registry.all_of<CameraComponent>(selectedEntity)) {
                registry.patch<CameraComponent>(selectedEntity, [&](auto &cc) {
                    cc.camera.setViewYXZ(transform.translation, transform.rotation);
                });
            }
        }

        if (objectTransformEditActive
            && objectTransformEditEntity == selectedEntity
            && !ImViewGuizmo::IsTransformUsing()) {
            history.recordTransform(selectedEntity, objectTransformEditBefore, transform);
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

        glm::vec3 camPos = transform.translation;
        if (ImViewGuizmo::Rotate(camPos, cameraRotation, transform.translation, gizmoPos, 0.008f)) {
            const glm::vec3 newForward = glm::normalize(cameraRotation * ImViewGuizmo::worldForward);
            registry.patch<TransformComponent>(cameraEntity, [&](auto &tc) {
                tc.translation = transform.translation;
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
        registry.emplace<SceneNodeComponent>(entity, std::move(node));

        TransformComponent transform{};
        transform.translation = primitiveSpawnPosition();
        registry.emplace<TransformComponent>(entity, transform);

        ModelComponent model{};
        model.meshHandle = primitiveMesh(primitive);
        registry.emplace<ModelComponent>(entity, model);

        MaterialComponent material{};
        auto mat = std::make_shared<Material>();
        mat->name = primitiveName(primitive) + " Material";
        mat->baseColor = glm::vec4{0.82f, 0.82f, 0.78f, 1.0f};
        mat->baseColorTexture = primitiveWhiteTexture();
        material.materialHandle = projectLayer.assetManager().store<Material>(
            std::move(mat), "##editor/materials/" + mat->name);
        registry.emplace<MaterialComponent>(entity, material);

        selectedEntity = entity;
    }

    void ViewportPanel::addLight(const LightType type) {
        auto *scene = projectLayer.project().scene();
        if (!scene) return;

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
        // Prefer editor camera
        for (const entt::entity e: registry.view<TransformComponent, CameraComponent, EditorCameraComponent>()) {
            if (const auto *n = registry.try_get<SceneNodeComponent>(e); n && (n->deleted || !n->visible)) continue;
            return e;
        }
        // Fall back to first scene camera
        for (const entt::entity e: registry.view<TransformComponent, CameraComponent>()) {
            if (const auto *n = registry.try_get<SceneNodeComponent>(e); n && (n->deleted || !n->visible)) continue;
            if (registry.all_of<TransientComponent>(e)) continue;
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
