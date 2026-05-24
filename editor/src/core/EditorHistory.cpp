#include "EditorHistory.hpp"

#include <utility>

namespace Atlas::Editor {
    bool EditorHistory::canUndo() const {
        return !undoStack.empty();
    }

    bool EditorHistory::canRedo() const {
        return !redoStack.empty();
    }

    void EditorHistory::clear() {
        undoStack.clear();
        redoStack.clear();
    }

    void EditorHistory::undo(entt::registry &registry) {
        if (undoStack.empty()) {
            return;
        }

        Entry entry = undoStack.back();
        undoStack.pop_back();
        apply(registry, entry, false);
        redoStack.push_back(std::move(entry));
    }

    void EditorHistory::redo(entt::registry &registry) {
        if (redoStack.empty()) {
            return;
        }

        Entry entry = redoStack.back();
        redoStack.pop_back();
        apply(registry, entry, true);
        undoStack.push_back(std::move(entry));
    }

    void EditorHistory::recordName(entt::entity entity, std::string before, std::string after) {
        if (before == after) {
            return;
        }

        Entry entry{.type = EntryType::Name, .entity = entity};
        entry.beforeName = std::move(before);
        entry.afterName = std::move(after);
        push(std::move(entry));
    }

    void EditorHistory::recordTransform(entt::entity entity, const TransformComponent &before, const TransformComponent &after) {
        if (before.translation == after.translation && before.rotation == after.rotation && before.scale == after.scale) {
            return;
        }

        Entry entry{.type = EntryType::Transform, .entity = entity};
        entry.beforeTransform = before;
        entry.afterTransform = after;
        push(std::move(entry));
    }

    void EditorHistory::recordMaterial(entt::entity entity, const MaterialComponent &before, const MaterialComponent &after) {
        if (before.baseColor == after.baseColor &&
            before.albedoTexture == after.albedoTexture &&
            before.normalMap == after.normalMap &&
            before.metallicRoughnessMap == after.metallicRoughnessMap &&
            before.ambientOcclusion == after.ambientOcclusion &&
            before.alphaMasked == after.alphaMasked &&
            before.transparent == after.transparent) {
            return;
        }

        Entry entry{.type = EntryType::Material, .entity = entity};
        entry.beforeMaterial = before;
        entry.afterMaterial = after;
        push(std::move(entry));
    }

    void EditorHistory::recordLight(entt::entity entity, const LightComponent &before, const LightComponent &after) {
        if (before.type == after.type &&
            before.color == after.color &&
            before.intensity == after.intensity &&
            before.range == after.range &&
            before.direction == after.direction &&
            before.innerConeAngle == after.innerConeAngle &&
            before.outerConeAngle == after.outerConeAngle &&
            before.width == after.width &&
            before.height == after.height &&
            before.rectRight == after.rectRight &&
            before.rectUp == after.rectUp) {
            return;
        }

        Entry entry{.type = EntryType::Light, .entity = entity};
        entry.beforeLight = before;
        entry.afterLight = after;
        push(std::move(entry));
    }

    void EditorHistory::push(Entry entry) {
        undoStack.push_back(std::move(entry));
        redoStack.clear();
    }

    void EditorHistory::apply(entt::registry &registry, const Entry &entry, const bool redo) {
        if (entry.entity == entt::null || !registry.valid(entry.entity)) {
            return;
        }

        switch (entry.type) {
            case EntryType::Name:
                if (auto *node = registry.try_get<SceneNodeComponent>(entry.entity)) {
                    node->name = redo ? entry.afterName : entry.beforeName;
                }
                break;
            case EntryType::Transform:
                if (registry.all_of<TransformComponent>(entry.entity)) {
                    registry.patch<TransformComponent>(entry.entity, [&](auto &transform) {
                        transform = redo ? entry.afterTransform : entry.beforeTransform;
                    });
                    refreshCamera(registry, entry.entity);
                }
                break;
            case EntryType::Material:
                if (registry.all_of<MaterialComponent>(entry.entity)) {
                    registry.patch<MaterialComponent>(entry.entity, [&](auto &material) {
                        material = redo ? entry.afterMaterial : entry.beforeMaterial;
                    });
                }
                break;
            case EntryType::Light:
                if (registry.all_of<LightComponent>(entry.entity)) {
                    registry.patch<LightComponent>(entry.entity, [&](auto &light) {
                        light = redo ? entry.afterLight : entry.beforeLight;
                    });
                }
                break;
        }
    }

    void EditorHistory::refreshCamera(entt::registry &registry, const entt::entity entity) {
        if (!registry.all_of<TransformComponent, CameraComponent>(entity)) {
            return;
        }

        const auto &transform = registry.get<TransformComponent>(entity);
        registry.patch<CameraComponent>(entity, [&](auto &camera) {
            camera.camera.setViewYXZ(transform.translation, transform.rotation);
        });
    }
}
