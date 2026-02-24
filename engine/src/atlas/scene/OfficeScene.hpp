#pragma once

#include <memory>

#include "Scene.hpp"
#include "renderer/Renderer.hpp"
#include "system/CameraSystem.hpp"
#include "system/RenderSystem.hpp"
#include "system/RenderSystemV2.hpp"
#include "system/SkyboxSystem.hpp"

#ifdef _WIN32
#endif

namespace Atlas {
    struct alignas(16) GlobalUbo {
        Camera::Data cameraData;
        glm::vec4 ambientColor{1.0f, 1.0f, 1.0f, 0.002f};
        glm::vec3 lightPosition{-1.0f};
        float padding1;
        glm::vec4 lightColor{1.0f};
    };

    class OfficeScene : public Scene {
    public:
        explicit OfficeScene(Renderer &renderer);
        ~OfficeScene() override = default;

        void onLoad(entt::registry&& registry) override;
        void onUpdate(float deltaTime) override;
        void onRender(float deltaTime, float aspectRatio) override;
        void onDelete() override;
    protected:

        // Global descriptors (UBO)
        std::unique_ptr<DescriptorSetLayout> globalSetLayout;
        std::unique_ptr<DescriptorPool> globalPool;
        std::vector<std::unique_ptr<Buffer>> uboBuffers;
        std::vector<VkDescriptorSet> globalDescriptorSets;

        // Systems
        std::unique_ptr<CameraSystem> cameraSystem;
        std::unique_ptr<RenderSystemV2> renderSystem;
        std::unique_ptr<SkyboxSystem> skyboxSystem;
    };
}
