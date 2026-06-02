#include "HierarchyPanel.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include <imgui.h>

namespace Atlas::Editor {
    HierarchyPanel::HierarchyPanel(
        ProjectLayer &projectLayer,
        entt::entity &selectedEntity,
        std::vector<entt::entity> &selectedEntities,
        EditorHistory &history)
        : projectLayer(projectLayer), selectedEntity(selectedEntity), selectedEntities(selectedEntities), history(history) {
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
        pruneSelection(registry);
        auto view = registry.view<SceneNodeComponent>();

        if (ImGui::Button("Add Entity", ImVec2(-1.0f, 0.0f))) {
            selectedEntity = createEntity(registry);
            selectedEntities = {selectedEntity};
        }
        ImGui::Separator();

        const float iconColumnWidth = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
        if (ImGui::BeginTable("HierarchyRows", 3, ImGuiTableFlags_SizingStretchProp, ImVec2(0.0f, 0.0f))) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Visibility", ImGuiTableColumnFlags_WidthFixed, iconColumnWidth);
            ImGui::TableSetupColumn("Delete", ImGuiTableColumnFlags_WidthFixed, iconColumnWidth);

            for (auto entity: view) {
                if (registry.all_of<TransientComponent>(entity)) {
                    continue;
                }

                const auto &node = view.get<SceneNodeComponent>(entity);
                if (!node.deleted && node.parent == entt::null) {
                    drawEntityNode(registry, entity);
                }
            }

            ImGui::EndTable();
        }

        if (pendingDeleteEntity != entt::null) {
            const entt::entity entity = pendingDeleteEntity;
            pendingDeleteEntity = entt::null;
            if (registry.valid(entity)) {
                deleteEntity(registry, entity);
            }
        }

        ImGui::End();
    }

    void HierarchyPanel::deleteSelected() {
        if (selectedEntities.empty() && selectedEntity == entt::null) {
            return;
        }

        auto *scene = projectLayer.project().scene();
        if (!scene) {
            return;
        }

        auto &registry = scene->getRegistry();
        pruneSelection(registry);

        if (selectedEntities.empty() && selectedEntity != entt::null && registry.valid(selectedEntity)) {
            selectedEntities = {selectedEntity};
        }

        if (selectedEntities.empty()) {
            selectedEntity = entt::null;
            return;
        }

        const std::vector<entt::entity> deleteList = selectedEntities;
        for (const entt::entity entity: deleteList) {
            if (registry.valid(entity) && !registry.all_of<TransientComponent>(entity)) {
                deleteEntity(registry, entity);
            }
        }

        selectedEntities.clear();
        selectedEntity = entt::null;
    }

    entt::entity HierarchyPanel::createEntity(entt::registry &registry) {
        const entt::entity entity = registry.create();

        SceneNodeComponent node{};
        node.name = nextEntityName(registry);
        registry.emplace<SceneNodeComponent>(entity, std::move(node));
        registry.emplace<TransformComponent>(entity);
        registry.patch<TransformComponent>(entity);

        return entity;
    }

    std::string HierarchyPanel::nextEntityName(entt::registry &registry) const {
        int nextIndex = 1;
        for (const entt::entity entity: registry.view<SceneNodeComponent>()) {
            if (registry.all_of<TransientComponent>(entity)) {
                continue;
            }

            const auto &node = registry.get<SceneNodeComponent>(entity);
            if (!node.deleted) {
                ++nextIndex;
            }
        }

        return "Entity " + std::to_string(nextIndex);
    }

    void HierarchyPanel::drawEntityNode(entt::registry &registry, entt::entity entity) {
        auto &node = registry.get<SceneNodeComponent>(entity);
        if (node.deleted) {
            return;
        }
        if (registry.all_of<TransientComponent>(entity)) {
            return;
        }

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (isSelected(entity)) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }
        if (node.children.empty()) {
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();

        if (!node.visible) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        }

        const bool open = ImGui::TreeNodeEx(
            reinterpret_cast<void *>(static_cast<uintptr_t>(entt::to_integral(entity))),
            flags,
            "%s",
            entityName(registry, entity)
        );

        if (!node.visible) {
            ImGui::PopStyleColor();
        }

        if (ImGui::IsItemClicked()) {
            selectEntity(registry, entity);
        }

        ImGui::TableNextColumn();
        drawVisibilityButton(registry, entity);

        ImGui::TableNextColumn();
        drawDeleteButton(entity);

        if (open && !node.children.empty()) {
            for (auto child: node.children) {
                if (registry.valid(child) &&
                    registry.all_of<SceneNodeComponent>(child) &&
                    !registry.all_of<TransientComponent>(child) &&
                    !registry.get<SceneNodeComponent>(child).deleted) {
                    drawEntityNode(registry, child);
                }
            }

            ImGui::TreePop();
        }
    }

    void HierarchyPanel::selectEntity(entt::registry &registry, const entt::entity entity) {
        if (!registry.valid(entity) || registry.all_of<TransientComponent>(entity)) {
            return;
        }

        selectedEntity = entity;
        const ImGuiIO &io = ImGui::GetIO();
        if (io.KeyCtrl) {
            const auto it = std::ranges::find(selectedEntities, entity);
            if (it == selectedEntities.end()) {
                selectedEntities.push_back(entity);
            } else {
                selectedEntities.erase(it);
                if (selectedEntities.empty()) {
                    selectedEntity = entt::null;
                } else {
                    selectedEntity = selectedEntities.back();
                }
            }
            return;
        }

        selectedEntities = {entity};
    }

    bool HierarchyPanel::isSelected(const entt::entity entity) const {
        return std::ranges::find(selectedEntities, entity) != selectedEntities.end();
    }

    void HierarchyPanel::pruneSelection(entt::registry &registry) {
        selectedEntities.erase(
            std::remove_if(selectedEntities.begin(), selectedEntities.end(), [&](const entt::entity entity) {
                if (entity == entt::null || !registry.valid(entity) || registry.all_of<TransientComponent>(entity)) {
                    return true;
                }

                const auto *node = registry.try_get<SceneNodeComponent>(entity);
                return node && node->deleted;
            }),
            selectedEntities.end());

        if (selectedEntity != entt::null) {
            const bool primaryStillSelected = std::ranges::find(selectedEntities, selectedEntity) != selectedEntities.end();
            if (!primaryStillSelected) {
                if (!registry.valid(selectedEntity) || registry.all_of<TransientComponent>(selectedEntity)) {
                    selectedEntity = entt::null;
                } else if (const auto *node = registry.try_get<SceneNodeComponent>(selectedEntity); node && node->deleted) {
                    selectedEntity = entt::null;
                } else {
                    selectedEntities = {selectedEntity};
                }
            }
        }

        if (selectedEntity == entt::null && !selectedEntities.empty()) {
            selectedEntity = selectedEntities.back();
        }
    }

    void HierarchyPanel::drawDeleteButton(entt::entity entity) {
        const float size = ImGui::GetFrameHeight();
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        const float x = cursor.x + (ImGui::GetContentRegionAvail().x - size) * 0.5f;

        ImGui::SetCursorScreenPos(ImVec2(x, cursor.y));
        ImGui::PushID(static_cast<int>(entt::to_integral(entity)));
        const bool clicked = ImGui::InvisibleButton("delete", ImVec2(size, size));
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 buttonMin = ImGui::GetItemRectMin();
        const ImVec2 buttonMax = ImGui::GetItemRectMax();
        ImGui::PopID();

        ImDrawList *drawList = ImGui::GetWindowDrawList();
        if (hovered) {
            drawList->AddRectFilled(buttonMin, buttonMax, ImGui::GetColorU32(ImGuiCol_HeaderHovered));
        }

        drawTrashIcon(*drawList, buttonMin, buttonMax, ImGui::GetColorU32(ImGuiCol_TextDisabled));

        if (clicked) {
            pendingDeleteEntity = entity;
        }
    }

    void HierarchyPanel::deleteEntity(entt::registry &registry, entt::entity entity) {
        if (!registry.valid(entity)) {
            return;
        }

        const bool affectsSelection = containsEntity(registry, entity, selectedEntity);
        if (auto *node = registry.try_get<SceneNodeComponent>(entity)) {
            const std::vector<entt::entity> children = node->children;
            for (const entt::entity child: children) {
                deleteEntity(registry, child);
            }

            if (node->parent != entt::null && registry.valid(node->parent)) {
                const entt::entity parent = node->parent;
                if (auto *parentNode = registry.try_get<SceneNodeComponent>(parent)) {
                    auto &siblings = parentNode->children;
                    siblings.erase(std::remove(siblings.begin(), siblings.end(), entity), siblings.end());
                    registry.patch<SceneNodeComponent>(parent);
                }
            }

            registry.patch<SceneNodeComponent>(entity, [](auto &sceneNode) {
                sceneNode.parent = entt::null;
                sceneNode.children.clear();
                sceneNode.visible = false;
                sceneNode.deleted = true;
            });
        }

        if (affectsSelection) {
            selectedEntity = entt::null;
        }
    }

    bool HierarchyPanel::containsEntity(entt::registry &registry, entt::entity root, entt::entity entity) {
        if (entity == entt::null || root == entt::null || !registry.valid(root)) {
            return false;
        }
        if (root == entity) {
            return true;
        }

        const auto *node = registry.try_get<SceneNodeComponent>(root);
        if (!node) {
            return false;
        }

        for (const entt::entity child: node->children) {
            if (containsEntity(registry, child, entity)) {
                return true;
            }
        }

        return false;
    }

    void HierarchyPanel::drawVisibilityButton(entt::registry &registry, entt::entity entity) {
        auto &node = registry.get<SceneNodeComponent>(entity);
        const float size = ImGui::GetFrameHeight();
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        const float x = cursor.x + (ImGui::GetContentRegionAvail().x - size) * 0.5f;

        ImGui::SetCursorScreenPos(ImVec2(x, cursor.y));
        ImGui::PushID(static_cast<int>(entt::to_integral(entity)));
        const bool clicked = ImGui::InvisibleButton("visibility", ImVec2(size, size));
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 buttonMin = ImGui::GetItemRectMin();
        const ImVec2 buttonMax = ImGui::GetItemRectMax();
        ImGui::PopID();

        ImDrawList *drawList = ImGui::GetWindowDrawList();
        if (hovered) {
            drawList->AddRectFilled(buttonMin, buttonMax, ImGui::GetColorU32(ImGuiCol_HeaderHovered));
        }

        const ImU32 iconColor = ImGui::GetColorU32(node.visible ? ImGuiCol_Text : ImGuiCol_TextDisabled);
        drawEyeIcon(*drawList, buttonMin, buttonMax, node.visible, iconColor);

        if (clicked) {
            const bool before = node.visible;
            registry.patch<SceneNodeComponent>(entity, [&](auto &sceneNode) {
                sceneNode.visible = !sceneNode.visible;
            });
            history.recordVisibility(entity, before, !before);
        }
    }

    void HierarchyPanel::drawEyeIcon(ImDrawList &drawList, const ImVec2 &min, const ImVec2 &max, const bool visible, const ImU32 color) {
        const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
        const float width = (max.x - min.x) * 0.56f;
        const float height = (max.y - min.y) * 0.28f;
        const float thickness = 1.4f;

        const ImVec2 left(center.x - width * 0.5f, center.y);
        const ImVec2 right(center.x + width * 0.5f, center.y);
        const ImVec2 upperControlA(center.x - width * 0.22f, center.y - height);
        const ImVec2 upperControlB(center.x + width * 0.22f, center.y - height);
        const ImVec2 lowerControlA(center.x - width * 0.22f, center.y + height);
        const ImVec2 lowerControlB(center.x + width * 0.22f, center.y + height);

        drawList.AddBezierCubic(left, upperControlA, upperControlB, right, color, thickness);
        drawList.AddBezierCubic(left, lowerControlA, lowerControlB, right, color, thickness);
        if (visible) {
            drawList.AddCircleFilled(center, height * 0.38f, color, 12);
        } else {
            drawList.AddLine(
                ImVec2(center.x - width * 0.46f, center.y + height * 1.05f),
                ImVec2(center.x + width * 0.46f, center.y - height * 1.05f),
                color,
                thickness
            );
        }
    }

    void HierarchyPanel::drawTrashIcon(ImDrawList &drawList, const ImVec2 &min, const ImVec2 &max, const ImU32 color) {
        const float width = max.x - min.x;
        const float height = max.y - min.y;
        const float thickness = 1.4f;
        const float left = min.x + width * 0.34f;
        const float right = min.x + width * 0.66f;
        const float top = min.y + height * 0.36f;
        const float bottom = min.y + height * 0.74f;
        const float lidY = min.y + height * 0.30f;

        drawList.AddLine(ImVec2(left - width * 0.04f, lidY), ImVec2(right + width * 0.04f, lidY), color, thickness);
        drawList.AddLine(ImVec2(left + width * 0.08f, lidY - height * 0.08f), ImVec2(right - width * 0.08f, lidY - height * 0.08f), color, thickness);
        drawList.AddLine(ImVec2(left, top), ImVec2(left + width * 0.04f, bottom), color, thickness);
        drawList.AddLine(ImVec2(right, top), ImVec2(right - width * 0.04f, bottom), color, thickness);
        drawList.AddLine(ImVec2(left + width * 0.04f, bottom), ImVec2(right - width * 0.04f, bottom), color, thickness);
        drawList.AddLine(ImVec2(min.x + width * 0.46f, top + height * 0.08f), ImVec2(min.x + width * 0.46f, bottom - height * 0.08f), color, thickness);
        drawList.AddLine(ImVec2(min.x + width * 0.54f, top + height * 0.08f), ImVec2(min.x + width * 0.54f, bottom - height * 0.08f), color, thickness);
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
