#include "EditorLayer.hpp"

#include <Atlas.hpp>

#include "imgui.h"

namespace Atlas::Editor {
    EditorLayer::EditorLayer(ProjectLayer &projectLayer)
        : Layer("EditorLayer"), projectLayer(projectLayer) {
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
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("Project")) {
                ImGui::MenuItem("Open", "Ctrl+O", false, false);
                ImGui::MenuItem("Save", "Ctrl+S", false, false);
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

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

        ImGui::Begin("Stats");
        ImGui::Text("Frame %.3f ms", frameTime * 1000.0f);
        ImGui::Text("FPS %.1f", frameTime > 0.0f ? 1.0f / frameTime : 0.0f);
        ImGui::End();
    }

    void EditorLayer::drawRenderSettings() {
        ImGui::Begin("Render Settings");

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
        if (size.x > 1.0f && size.y > 1.0f) {
            projectLayer.getRenderer().setSceneViewportExtent({
                static_cast<uint32_t>(size.x),
                static_cast<uint32_t>(size.y)
            });
        }

        if (viewportTexture != VK_NULL_HANDLE && size.x > 1.0f && size.y > 1.0f) {
            ImGui::Image((ImTextureID) viewportTexture, size);
        }

        ImGui::End();
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
