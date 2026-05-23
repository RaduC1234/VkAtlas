#include "HierarchyPanel.hpp"

#include <cstdint>

#include <imgui.h>

namespace Atlas::Editor {
    HierarchyPanel::HierarchyPanel(ProjectLayer &projectLayer, entt::entity &selectedEntity) : projectLayer(projectLayer), selectedEntity(selectedEntity) {
    }

    void HierarchyPanel::onImGuiRender() {
        if (!visible) {
            return;
        }

        ImGui::Begin("Scene Hierarchy", &visible);

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

    void HierarchyPanel::drawEntityNode(entt::registry &registry, entt::entity entity) {
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

    const char *HierarchyPanel::entityName(entt::registry &registry, entt::entity entity) const {
        if (registry.valid(entity) && registry.all_of<SceneNodeComponent>(entity)) {
            const auto &node = registry.get<SceneNodeComponent>(entity);
            if (!node.name.empty()) {
                return node.name.c_str();
            }
        }

        return "Entity";
    }
}
