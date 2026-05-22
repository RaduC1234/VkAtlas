#include "EditorLayer.hpp"

#include <Atlas.hpp>

#include <ImGuizmo.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "imgui.h"

namespace Atlas::Editor {
    namespace {
        constexpr float TITLEBAR_HEIGHT = 33.0f;

        glm::vec3 decomposeScale(const glm::mat4 &matrix) {
            return {
                glm::length(glm::vec3(matrix[0])),
                glm::length(glm::vec3(matrix[1])),
                glm::length(glm::vec3(matrix[2]))
            };
        }

        glm::vec3 decomposeRotationYXZ(const glm::mat4 &matrix, const glm::vec3 &scale) {
            glm::mat3 rotationMatrix{1.0f};
            if (scale.x != 0.0f) {
                rotationMatrix[0] = glm::vec3(matrix[0]) / scale.x;
            }
            if (scale.y != 0.0f) {
                rotationMatrix[1] = glm::vec3(matrix[1]) / scale.y;
            }
            if (scale.z != 0.0f) {
                rotationMatrix[2] = glm::vec3(matrix[2]) / scale.z;
            }

            const float x = glm::asin(glm::clamp(-rotationMatrix[2][1], -1.0f, 1.0f));
            const float cosX = glm::cos(x);

            if (glm::abs(cosX) > 0.0001f) {
                return {
                    x,
                    glm::atan(rotationMatrix[2][0], rotationMatrix[2][2]),
                    glm::atan(rotationMatrix[0][1], rotationMatrix[1][1])
                };
            }

            return {
                x,
                glm::atan(-rotationMatrix[0][2], rotationMatrix[0][0]),
                0.0f
            };
        }

        ImGuizmo::OPERATION toImGuizmoOperation(int operation) {
            switch (operation) {
                case 1:
                    return ImGuizmo::ROTATE;
                case 2:
                    return ImGuizmo::SCALE;
                default:
                    return ImGuizmo::TRANSLATE;
            }
        }
    }

    EditorLayer::EditorLayer(ProjectLayer &projectLayer) : Layer("EditorLayer"), projectLayer(projectLayer) {
    }

    void EditorLayer::onAttach() {
    }

    void EditorLayer::onDetach() {
        destroyViewportTexture();
    }

    void EditorLayer::onUpdate(float deltaTime) {
        frameTime = deltaTime;
    }

    void EditorLayer::onImGuiRender() {
        ImGuizmo::BeginFrame();

        if (ImGui::IsKeyPressed(ImGuiKey_W)) {
            gizmoOperation = 0;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_E)) {
            gizmoOperation = 1;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_R)) {
            gizmoOperation = 2;
        }

        drawTitleBar();
        drawRenderSettings();
        drawViewport();
        drawSceneHierarchy();

        ImGui::Begin("Inspector");
        if (selectedEntity == entt::null) {
            ImGui::Text("No entity selected");
        } else if (auto *scene = projectLayer.project().scene()) {
            auto &registry = scene->getRegistry();
            ImGui::Text("%s", entityName(registry, selectedEntity));
        }
        ImGui::End();
    }

    void EditorLayer::drawRenderSettings() {
        ImGui::Begin("Render Settings");
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

    void EditorLayer::createViewportTexture() {
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
        viewportImageView = outputImage.imageView;
        viewportImageLayout = outputImage.imageLayout;
        viewportTexture = ImGuiLayer::addTexture(outputImage.imageView, outputImage.imageLayout);
    }

    void EditorLayer::destroyViewportTexture() {
        if (viewportTexture != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(projectLayer.getRenderer().device().device());
            ImGuiLayer::removeTexture(viewportTexture);
            viewportTexture = VK_NULL_HANDLE;
        }

        viewportImageView = VK_NULL_HANDLE;
        viewportImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    void EditorLayer::drawViewport() {
        createViewportTexture();

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("Viewport");
        ImGui::PopStyleVar();

        const ImVec2 size = ImGui::GetContentRegionAvail();
        const ImVec2 viewportMin = ImGui::GetCursorScreenPos();
        if (size.x > 1.0f && size.y > 1.0f) {
            projectLayer.getRenderer().setSceneViewportExtent({
                static_cast<uint32_t>(size.x),
                static_cast<uint32_t>(size.y)
            });
        }

        if (viewportTexture != VK_NULL_HANDLE && size.x > 1.0f && size.y > 1.0f) {
            ImGui::Image((ImTextureID) viewportTexture, size);
        }

        drawViewportGizmo(viewportMin, size);

        ImGui::End();
    }

    void EditorLayer::drawTitleBar() {
        ImGuiViewport *viewport = ImGui::GetMainViewport();

        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, TITLEBAR_HEIGHT));
        ImGui::SetNextWindowViewport(viewport->ID);

        constexpr ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoBringToFrontOnFocus;

        ImGui::Begin("AtlasTitleBar", nullptr, flags);

        ImDrawList *drawList = ImGui::GetWindowDrawList();
        const ImVec2 windowPos = ImGui::GetWindowPos();
        const ImVec2 windowSize = ImGui::GetWindowSize();
        drawList->AddRectFilled(
            windowPos,
            ImVec2(windowPos.x + windowSize.x, windowPos.y + windowSize.y),
            IM_COL32(5, 5, 5, 255)
        );
        drawList->AddLine(
            ImVec2(windowPos.x, windowPos.y + TITLEBAR_HEIGHT - 1.0f),
            ImVec2(windowPos.x + windowSize.x, windowPos.y + TITLEBAR_HEIGHT - 1.0f),
            IM_COL32(41, 41, 41, 255)
        );

        const float frameHeight = ImGui::GetFrameHeight();
        ImGui::SetCursorPos(ImVec2(8.0f, (TITLEBAR_HEIGHT - frameHeight) * 0.5f));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Atlas");

        auto drawMenuButton = [](const char *label, const char *popupId, float x) {
            ImGui::SameLine(x, 0.0f);
            if (ImGui::Button(label)) {
                ImGui::OpenPopup(popupId);
            }
        };

        drawMenuButton("File", "AtlasTitleBar.File", 50.0f);
        if (ImGui::BeginPopup("AtlasTitleBar.File")) {
            ImGui::MenuItem("Open", "Ctrl+O");
            ImGui::MenuItem("Save", "Ctrl+S");
            ImGui::Separator();
            ImGui::MenuItem("Exit");
            ImGui::EndPopup();
        }

        drawMenuButton("Edit", "AtlasTitleBar.Edit", 94.0f);
        if (ImGui::BeginPopup("AtlasTitleBar.Edit")) {
            ImGui::MenuItem("Undo", "Ctrl+Z");
            ImGui::MenuItem("Redo", "Ctrl+Y");
            ImGui::EndPopup();
        }

        drawMenuButton("View", "AtlasTitleBar.View", 140.0f);
        if (ImGui::BeginPopup("AtlasTitleBar.View")) {
            ImGui::MenuItem("Scene Hierarchy");
            ImGui::MenuItem("Inspector");
            ImGui::MenuItem("Render Settings");
            ImGui::EndPopup();
        }

        drawMenuButton("Tools", "AtlasTitleBar.Tools", 190.0f);
        if (ImGui::BeginPopup("AtlasTitleBar.Tools")) {
            ImGui::MenuItem("Content Browser");
            ImGui::EndPopup();
        }

        drawMenuButton("Help", "AtlasTitleBar.Help", 248.0f);
        if (ImGui::BeginPopup("AtlasTitleBar.Help")) {
            ImGui::MenuItem("About Atlas");
            ImGui::EndPopup();
        }

        const char *title = "Atlas Editor";
        const float titleWidth = ImGui::CalcTextSize(title).x;
        const float titleX = (viewport->Size.x - titleWidth) * 0.5f;
        const float titleY = (TITLEBAR_HEIGHT - ImGui::GetTextLineHeight()) * 0.5f;

        drawList->AddText(
            ImVec2(windowPos.x + titleX, windowPos.y + titleY),
            IM_COL32(190, 190, 190, 255),
            title
        );

        ImGui::End();
    }

    void EditorLayer::drawViewportGizmo(const ImVec2 &viewportMin, const ImVec2 &viewportSize) {
        auto *scene = projectLayer.project().scene();
        if (!scene || selectedEntity == entt::null || viewportSize.x <= 1.0f || viewportSize.y <= 1.0f) {
            return;
        }

        auto &registry = scene->getRegistry();
        if (!registry.valid(selectedEntity) || !registry.all_of<TransformComponent>(selectedEntity)) {
            return;
        }

        auto cameraView = registry.view<CameraComponent>();
        if (cameraView.empty()) {
            return;
        }

        const auto cameraEntity = *cameraView.begin();
        const auto &camera = cameraView.get<CameraComponent>(cameraEntity).camera;
        auto &transform = registry.get<TransformComponent>(selectedEntity);

        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(viewportMin.x, viewportMin.y, viewportSize.x, viewportSize.y);

        glm::mat4 transformMatrix = transform.mat4();
        const glm::mat4 view = camera.getView();
        const glm::mat4 projection = camera.getProjection();

        if (ImGuizmo::Manipulate(
                glm::value_ptr(view),
                glm::value_ptr(projection),
                toImGuizmoOperation(gizmoOperation),
                ImGuizmo::LOCAL,
                glm::value_ptr(transformMatrix))) {
            transform.translation = glm::vec3(transformMatrix[3]);
            transform.scale = decomposeScale(transformMatrix);
            transform.rotation = decomposeRotationYXZ(transformMatrix, transform.scale);

            registry.patch<TransformComponent>(selectedEntity);
        }
    }

    void EditorLayer::drawSceneHierarchy() {
        ImGui::Begin("Scene Hierarchy");

        auto *scene = projectLayer.project().scene();
        if (!scene) {
            ImGui::End();
            return;
        }

        auto &registry = scene->getRegistry();
        auto view = registry.view<SceneNodeComponent>();

        for (auto entity: view) {
            const auto &node = view.get<SceneNodeComponent>(entity);
            if (node.parent == entt::null) {
                drawEntityNode(registry, entity);
            }
        }

        ImGui::End();
    }

    void EditorLayer::drawEntityNode(entt::registry &registry, entt::entity entity) {
        auto &node = registry.get<SceneNodeComponent>(entity);

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (selectedEntity == entity) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }
        if (node.children.empty()) {
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }

        const bool open = ImGui::TreeNodeEx(
            reinterpret_cast<void *>(static_cast<uintptr_t>(entt::to_integral(entity))),
            flags,
            "%s",
            entityName(registry, entity)
        );

        if (ImGui::IsItemClicked()) {
            selectedEntity = entity;
        }

        if (open && !node.children.empty()) {
            for (auto child: node.children) {
                if (registry.valid(child) && registry.all_of<SceneNodeComponent>(child)) {
                    drawEntityNode(registry, child);
                }
            }

            ImGui::TreePop();
        }
    }

    const char *EditorLayer::entityName(entt::registry &registry, entt::entity entity) const {
        if (registry.valid(entity) && registry.all_of<SceneNodeComponent>(entity)) {
            const auto &node = registry.get<SceneNodeComponent>(entity);
            if (!node.name.empty()) {
                return node.name.c_str();
            }
        }

        return "Entity";
    }
}
