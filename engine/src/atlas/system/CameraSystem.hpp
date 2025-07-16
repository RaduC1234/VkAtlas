#pragma once
#include <entt/entity/registry.hpp>
#include <glm/vec2.hpp>

#include "core/Keyboard.hpp"
#include "core/Window.hpp"

namespace Atlas {
    class CameraSystem {
    public:
        struct KeyMappings {
            KeyCode left = Keyboard::A;
            KeyCode right = Keyboard::D;
            KeyCode forward = Keyboard::W;
            KeyCode backward = Keyboard::S;

            KeyCode lockCamera = Mouse::ButtonRight;
        };

        CameraSystem(Window &window);

        void update(entt::registry &registry, float deltaTime) const;;

    private:
        Window &window;
        KeyMappings keyMappings{};

        mutable glm::vec2 lastMousePosition{};
        float moveSpeed{2.0f};
        float lookSpeed{1.5f};
        float mouseSens{0.25};
    };
}
