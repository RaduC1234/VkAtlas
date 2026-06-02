#include "CameraSystem.hpp"

#include <algorithm>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include "core/Log.hpp"
#include "entity/Object.hpp"


namespace Atlas {
    CameraSystem::CameraSystem(Window &window): window{window} {
        auto [x, y] = Mouse::getCursorPosition();
        this->lastMousePosition = {x, y};
    }

    void CameraSystem::update(entt::registry &registry, float deltaTime, float screenAspect) {
        constexpr float maxMovementDeltaTime = 1.0f / 30.0f;
        constexpr float referenceLookDeltaTime = 1.0f / 60.0f;
        const float movementDeltaTime = std::min(deltaTime, maxMovementDeltaTime);

        auto [mx, my] = Mouse::getCursorPosition();
        glm::vec2 curPos = {mx, my};

        glm::vec2 delta{0.0f};
        if (Mouse::isButtonPressed(keyMappings.lockCamera)) {
            if (!locked) {
                window.setCursorMode(Window::CursorMode::Disabled);
                lastMousePosition = curPos;
                locked = true;
            }
            delta = curPos - lastMousePosition;
            lastMousePosition = curPos;
        } else {
            if (locked) {
                window.setCursorMode(Window::CursorMode::Normal);
                locked = false;
            }
        }

        glm::vec3 rotationDt{
            -delta.y * mouseSens,
            delta.x * mouseSens,
            0.0f
        };

        auto isCameraUsable = [&](entt::entity entity) {
            if (const auto *node = registry.try_get<SceneNodeComponent>(entity); node && (node->deleted || !node->visible)) {
                return false;
            }
            return true;
        };

        // Prefer scene cameras; fall back to editor camera if none exist
        bool hasSceneCamera = false;
        for (const entt::entity entity: registry.view<TransformComponent, CameraComponent>()) {
            if (registry.all_of<TransientComponent>(entity)) continue;
            if (!isCameraUsable(entity)) continue;
            hasSceneCamera = true;
            break;
        }

        auto updateCamera = [&](entt::entity entity, TransformComponent &tf) {
            if (locked) {
                if (glm::dot(rotationDt, rotationDt) > std::numeric_limits<float>::epsilon()) {
                    tf.rotation += lookSpeed * referenceLookDeltaTime * rotationDt;
                }

                tf.rotation.x = glm::clamp(tf.rotation.x, -1.5f, 1.5f);
                tf.rotation.y = glm::mod(tf.rotation.y, glm::two_pi<float>());

                const float pitch = tf.rotation.x;
                const float yaw = tf.rotation.y;

                glm::vec3 forward{
                    cos(pitch) * sin(yaw),
                    -sin(pitch),
                    cos(pitch) * cos(yaw)
                };
                forward = glm::normalize(forward);

                glm::vec3 worldUp{0.0f, 1.0f, 0.0f};
                if (std::abs(forward.y) > 0.99f) {
                    worldUp = {1.0f, 0.0f, 0.0f};
                }

                glm::vec3 right = glm::normalize(glm::cross(worldUp, forward));

                glm::vec3 dir{0.0f};
                if (Keyboard::isKeyPressed(keyMappings.forward)) dir += forward;
                if (Keyboard::isKeyPressed(keyMappings.backward)) dir -= forward;
                if (Keyboard::isKeyPressed(keyMappings.right)) dir += right;
                if (Keyboard::isKeyPressed(keyMappings.left)) dir -= right;

                if (glm::dot(dir, dir) > std::numeric_limits<float>::epsilon()) {
                    tf.translation += moveSpeed * movementDeltaTime * glm::normalize(dir);
                }

                registry.patch<TransformComponent>(entity);
            }

            registry.patch<CameraComponent>(entity, [&](auto &camComp) {
                camComp.camera.setViewYXZ(tf.translation, tf.rotation);
                const float nearZ = std::max(camComp.nearPlane, 0.001f);
                const float farZ = std::max(camComp.farPlane, nearZ + 0.01f);
                if (camComp.projection == CameraProjection::ORTHOGRAPHIC) {
                    const float halfHeight = std::max(camComp.orthographicHalfHeight, 0.001f);
                    camComp.camera.setOrthographicProjection(
                        -halfHeight * screenAspect,
                        halfHeight * screenAspect,
                        -halfHeight,
                        halfHeight,
                        nearZ,
                        farZ);
                } else {
                    camComp.camera.setPerspectiveProjection(camComp.perspectiveFovY, screenAspect, nearZ, farZ);
                }
            });
        };

        if (hasSceneCamera) {
            for (const entt::entity entity: registry.view<TransformComponent, CameraComponent>()) {
                if (registry.all_of<TransientComponent>(entity)) continue;
                if (!isCameraUsable(entity)) continue;
                auto &tf = registry.get<TransformComponent>(entity);
                updateCamera(entity, tf);
            }
        } else {
            for (const entt::entity entity: registry.view<TransformComponent, CameraComponent, EditorCameraComponent>()) {
                if (!isCameraUsable(entity)) continue;
                auto &tf = registry.get<TransformComponent>(entity);
                updateCamera(entity, tf);
            }
        }
    }
}
