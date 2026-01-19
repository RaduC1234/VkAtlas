#include "Scene.hpp"

#include "system/CameraSystem.hpp"
#include "system/RenderSystem.hpp"

#include <stdexcept>

#include "entity/Object.hpp"

namespace Atlas {
    Scene::Scene(Renderer &renderer) : renderer(renderer) {
    }

    void Scene::onLoad(entt::registry &&loadedRegistry) {
    }

    void Scene::onUpdate(float deltaTime) {
    }

    void Scene::onRender(float deltaTime, float aspectRatio) {
    }

    void Scene::onDelete() {
        registry.clear();
    }
}
