#include "CameraSystem.hpp"

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include "entity/Object.hpp"

namespace Atlas {
    CameraSystem::CameraSystem(Window &window): window{window} {
        auto [x, y] = Mouse::getCursorPosition();
        this->lastMousePosition = {x, y};
    }

    void CameraSystem::update(entt::registry &registry, float deltaTime) const {
        if (Mouse::isButtonPressed(keyMappings.lockCamera)) {
            window.setCursorMode(WINDOW_CURSOR_DISABLED);
        } else {
            window.setCursorMode(WINDOW_CURSOR_NORMAL);
            return;
        }

        auto [mx, my] = Mouse::getCursorPosition();
        glm::vec2 curPos = {mx, my};
        glm::vec2 delta = curPos - lastMousePosition;
        lastMousePosition = curPos;

        glm::vec3 rotationDt{
            -delta.y * mouseSens,
            delta.x * mouseSens,
            0.0f
        };

        auto view = registry.view<Transform, CameraComponent>();

        for (auto entity: view) {
            auto &tf = view.get<Transform>(entity);
            auto &camComp = view.get<CameraComponent>(entity);

            if (glm::dot(rotationDt, rotationDt) >
                std::numeric_limits<float>::epsilon()) {
                tf.rotation += lookSpeed * deltaTime * rotationDt;
            }

            tf.rotation.x = glm::clamp(tf.rotation.x, -1.5f, 1.5f);
            tf.rotation.y = glm::mod(tf.rotation.y, glm::two_pi<float>());

            float pitch = tf.rotation.x;
            float yaw = tf.rotation.y;

            glm::vec3 forward{
                cos(pitch) * sin(yaw), // x
                -sin(pitch), // y
                cos(pitch) * cos(yaw) // z
            };
            forward = glm::normalize(forward);

            glm::vec3 worldUp{0.0f, 1.0f, 0.0f};

            if (std::abs(forward.y) > 0.99f) {
                worldUp = {1.0f, 0.0f, 0.0f};
            }

            glm::vec3 right = glm::normalize(glm::cross(worldUp, forward));
            glm::vec3 up = glm::cross(right, forward);

            glm::vec3 dir{0.0f};
            if (Keyboard::isKeyPressed(keyMappings.forward)) dir += forward;
            if (Keyboard::isKeyPressed(keyMappings.backward)) dir -= forward;
            if (Keyboard::isKeyPressed(keyMappings.right)) dir += right;
            if (Keyboard::isKeyPressed(keyMappings.left)) dir -= right;
            //if (Keyboard::isKeyPressed(keyMappings.up))       dir += up;
            //if (Keyboard::isKeyPressed(keyMappings.down))     dir -= up;

            if (glm::dot(dir, dir) >
                std::numeric_limits<float>::epsilon()) {
                tf.translation += moveSpeed * deltaTime * glm::normalize(dir);
            }

            camComp.camera.setViewYXZ(tf.translation, tf.rotation);
        }
    }
}
