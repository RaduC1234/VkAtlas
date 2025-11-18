#include "Scene.hpp"

#include "Object.hpp"
#include "system/CameraSystem.hpp"
#include "system/RenderSystem.hpp"

#include <stdexcept>

namespace Atlas {
    Scene::Scene(Renderer &renderer) : renderer(renderer) {
        cameraSystem = std::make_unique<CameraSystem>(renderer.getWindow());
        renderSystem = std::make_unique<RenderSystem>(renderer.getDevice(), renderer.getSwapChainRenderPass());
    }

    void Scene::onLoad(entt::registry&& loadedRegistry) {
        registry = std::move(loadedRegistry);

        std::vector<entt::entity> cameraEntities;
        for (auto e : registry.view<CameraComponent>()) {
            cameraEntities.push_back(e);
        }

        for (auto entity : cameraEntities) {
            registry.remove<CameraComponent>(entity);
            registry.emplace<CameraComponent>(entity, camera);
        }

        bool hasCameraEntity = !cameraEntities.empty();

        if (!hasCameraEntity) {
            auto cameraObj = registry.create();
            registry.emplace<TransformComponent>(cameraObj);
            registry.emplace<CameraComponent>(cameraObj, camera);
        }
    }

    void Scene::onUpdate(float deltaTime) {
        float aspect = renderer.getAspectRatio();
        cameraSystem->update(registry, deltaTime, aspect);
        camera.setPerspectiveProjection(glm::radians(50.f), aspect, 0.1f, 200.f);
    }

    void Scene::onRender(float deltaTime, float aspectRatio) {
        renderSystem->prepareTextures(registry);

        int frameIndex = renderer.getFrameIndex();

        renderSystem->updateUBO(
            frameIndex,
            camera.getProjection(),
            camera.getView(),
            glm::vec4(1.0f, 1.0f, 1.0f, 1.f), // ambient color
            glm::vec3(-1.0f), // light position
            glm::vec4(1.0f) // light color
        );

        if (auto commandBuffer = renderer.getCurrentCommandBuffer()) {
            renderSystem->render(registry, commandBuffer, frameIndex);
        }
    }

    void Scene::onDelete() {
        registry.clear();
    }

    Scene Scene::loadSceneFromJson(const std::string &json) {
        throw std::runtime_error("Scene::loadSceneFromJson not yet implemented");
    }

    std::string Scene::saveSceneToJson(const Scene &scene) {
        throw std::runtime_error("Scene::saveSceneToJson not yet implemented");
    }
}
