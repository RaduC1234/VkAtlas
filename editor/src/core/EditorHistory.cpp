#include "EditorHistory.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include <glm/gtc/constants.hpp>

namespace Atlas::Editor {
    bool editorHistoryLightUsesDirection(const LightType type) {
        return type == LightType::SPOT || type == LightType::DIRECTIONAL || type == LightType::RECT;
    }

    glm::vec3 editorHistorySafeDirection(const glm::vec3 &direction, const glm::vec3 &fallback = {0.0f, -1.0f, 0.0f}) {
        const float lengthSquared = glm::dot(direction, direction);
        return lengthSquared > 1e-8f ? direction * glm::inversesqrt(lengthSquared) : fallback;
    }

    glm::vec3 editorHistoryLightDirectionFromTransform(const TransformComponent &transform) {
        const glm::vec3 forward{
            std::cos(transform.rotation.x) * std::sin(transform.rotation.y),
            -std::sin(transform.rotation.x),
            std::cos(transform.rotation.x) * std::cos(transform.rotation.y)
        };
        return editorHistorySafeDirection(-forward);
    }

    glm::vec3 editorHistoryTransformAxis(const TransformComponent &transform, const int column, const glm::vec3 &fallback) {
        const glm::mat4 matrix = transform.mat4();
        return editorHistorySafeDirection(glm::vec3(matrix[column]), fallback);
    }

    glm::vec3 editorHistoryTransformRotationFromLightDirection(const glm::vec3 &direction) {
        const glm::vec3 normalized = editorHistorySafeDirection(direction);
        const glm::vec3 forward = -normalized;
        return {
            std::asin(std::clamp(-forward.y, -1.0f, 1.0f)),
            glm::mod(std::atan2(forward.x, forward.z), glm::two_pi<float>()),
            0.0f
        };
    }

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

    void EditorHistory::recordVisibility(entt::entity entity, const bool before, const bool after) {
        if (before == after) {
            return;
        }

        Entry entry{.type = EntryType::Visibility, .entity = entity};
        entry.beforeVisible = before;
        entry.afterVisible = after;
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
        if (before.materialHandle == after.materialHandle && before.materialHandle.path() == after.materialHandle.path()) {
            return;
        }

        Entry entry{.type = EntryType::Material, .entity = entity};
        entry.beforeMaterial = before;
        entry.afterMaterial = after;
        push(std::move(entry));
    }

    void EditorHistory::recordMaterialAsset(entt::entity entity, AssetHandle<Material> material, const Material &before, const Material &after) {
        if (!material || before.getHash() == after.getHash()) {
            return;
        }

        Entry entry{.type = EntryType::MaterialAsset, .entity = entity};
        entry.materialHandle = material;
        entry.beforeMaterialAsset = before;
        entry.afterMaterialAsset = after;
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
        if (entry.type != EntryType::MaterialAsset && (entry.entity == entt::null || !registry.valid(entry.entity))) {
            return;
        }

        switch (entry.type) {
            case EntryType::Name:
                if (auto *node = registry.try_get<SceneNodeComponent>(entry.entity)) {
                    node->name = redo ? entry.afterName : entry.beforeName;
                }
                break;
            case EntryType::Visibility:
                if (registry.all_of<SceneNodeComponent>(entry.entity)) {
                    registry.patch<SceneNodeComponent>(entry.entity, [&](auto &node) {
                        node.visible = redo ? entry.afterVisible : entry.beforeVisible;
                    });
                }
                break;
            case EntryType::Transform:
                if (registry.all_of<TransformComponent>(entry.entity)) {
                    registry.patch<TransformComponent>(entry.entity, [&](auto &transform) {
                        transform = redo ? entry.afterTransform : entry.beforeTransform;
                    });
                    refreshCamera(registry, entry.entity);
                    refreshLightFromTransform(registry, entry.entity);
                }
                break;
            case EntryType::Material:
                if (registry.all_of<MaterialComponent>(entry.entity)) {
                    registry.patch<MaterialComponent>(entry.entity, [&](auto &material) {
                        material = redo ? entry.afterMaterial : entry.beforeMaterial;
                    });
                }
                break;
            case EntryType::MaterialAsset:
                if (Material *material = entry.materialHandle.get()) {
                    *material = redo ? entry.afterMaterialAsset : entry.beforeMaterialAsset;
                    auto view = registry.view<MaterialComponent>();
                    for (const entt::entity entity: view) {
                        if (view.get<MaterialComponent>(entity).materialHandle == entry.materialHandle) {
                            registry.patch<MaterialComponent>(entity);
                        }
                    }
                }
                break;
            case EntryType::Light:
                if (registry.all_of<LightComponent>(entry.entity)) {
                    registry.patch<LightComponent>(entry.entity, [&](auto &light) {
                        light = redo ? entry.afterLight : entry.beforeLight;
                    });
                    refreshTransformFromLight(registry, entry.entity);
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

    void EditorHistory::refreshLightFromTransform(entt::registry &registry, const entt::entity entity) {
        if (!registry.all_of<TransformComponent, LightComponent>(entity)) {
            return;
        }

        const auto &transform = registry.get<TransformComponent>(entity);
        registry.patch<LightComponent>(entity, [&](auto &light) {
            if (!editorHistoryLightUsesDirection(light.type)) {
                return;
            }

            light.direction = editorHistoryLightDirectionFromTransform(transform);
            if (light.type == LightType::RECT) {
                light.rectRight = editorHistoryTransformAxis(transform, 0, {1.0f, 0.0f, 0.0f});
                light.rectUp = editorHistoryTransformAxis(transform, 1, {0.0f, 1.0f, 0.0f});
            }
        });
    }

    void EditorHistory::refreshTransformFromLight(entt::registry &registry, const entt::entity entity) {
        if (!registry.all_of<TransformComponent, LightComponent>(entity)) {
            return;
        }

        const auto &light = registry.get<LightComponent>(entity);
        if (!editorHistoryLightUsesDirection(light.type)) {
            return;
        }

        registry.patch<TransformComponent>(entity, [&](auto &transform) {
            transform.rotation = editorHistoryTransformRotationFromLightDirection(light.direction);
        });
    }
}
