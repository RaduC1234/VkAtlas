#include "Scene.hpp"


namespace Atlas {
    Scene::Scene(Renderer &renderer) : renderer(renderer) {
    }

    void Scene::onLoad(entt::registry &&loadedRegistry) {
    }

    void Scene::onUpdate(float deltaTime) {
    }

    void Scene::onRender(ImGuiLayer &imguiLayer) {
    }

    void Scene::onDelete() {
        registry.clear();
    }
}
