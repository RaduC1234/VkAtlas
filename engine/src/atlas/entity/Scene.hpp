#pragma once
#include <entt/entt.hpp>

namespace Atlas {
    class Scene {
    public:

        virtual ~Scene() = 0;

        virtual void onStart() = 0;
        virtual void onUpdate(float deltaTime) = 0;

        entt::registry registry;
    };
}
