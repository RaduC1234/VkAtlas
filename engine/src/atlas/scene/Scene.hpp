#pragma once

#include <memory>

#include "renderer/Camera.hpp"
#include "renderer/Renderer.hpp"
#include "system/CameraSystem.hpp"
#include "system/RenderSystem.hpp"
#include "system/SkyboxSystem.hpp"

#ifdef _WIN32
#include <imgui.h>
#endif

namespace Atlas {
    struct alignas(16) GlobalUbo {
        glm::mat4 projection{1.0f};
        glm::mat4 view{1.0f};
        glm::vec4 ambientColor{1.0f, 1.0f, 1.0f, 0.002f};
        glm::vec3 lightPosition{-1.0f};
        float padding1;
        glm::vec4 lightColor{1.0f};
    };

    class Scene {
    public:
        explicit Scene(Renderer &renderer);
        virtual ~Scene() = default;

        virtual void onLoad(entt::registry&& registry);
        virtual void onUpdate(float deltaTime);
        virtual void onRender(float deltaTime, float aspectRatio);
        virtual void onDelete();

        static Scene loadSceneFromJson(const std::string &json);
        static std::string saveSceneToJson(const Scene &scene);
    protected:
        entt::registry registry;
        Renderer& renderer;
        Camera camera{};

        // Global descriptors (UBO)
        std::unique_ptr<DescriptorSetLayout> globalSetLayout;
        std::unique_ptr<DescriptorPool> globalPool;
        std::vector<std::unique_ptr<Buffer>> uboBuffers;
        std::vector<VkDescriptorSet> globalDescriptorSets;

        // Systems
        std::unique_ptr<CameraSystem> cameraSystem;
        std::unique_ptr<RenderSystem> renderSystem;
        std::unique_ptr<SkyboxSystem> skyboxSystem;
    };
}
