#pragma once
#include "Scene.hpp"
#include "system/CameraSystem.hpp"
#include "system/SkyboxSystem.hpp"

namespace Atlas {
    class PBRTestScene : public Scene {
    public:
        explicit PBRTestScene(Renderer &renderer);
        void onLoad(entt::registry &&registry) override;
        void onUpdate(float deltaTime) override;
        void onRender(float deltaTime, float aspectRatio) override;
        void onDelete() override;

    private:
        // Global descriptors (UBO)
        std::unique_ptr<DescriptorSetLayout> globalSetLayout;
        std::unique_ptr<DescriptorPool> globalPool;
        std::vector<std::unique_ptr<Buffer> > uboBuffers;
        std::vector<VkDescriptorSet> globalDescriptorSets;

        std::unique_ptr<CameraSystem> cameraSystem;
        std::unique_ptr<RenderSystem> renderSystem;
        std::unique_ptr<SkyboxSystem> skyboxSystem;

        // ImGui-controllable light settings
        glm::vec3 lightPosition{1.3f, 0.0f, 4.0f};
        glm::vec4 lightColor{1.0f, 1.0f, 1.0f, 1.0f};
        glm::vec4 ambientLightColor{0.1f, 0.1f, 0.1f, 0.2f};
        float lightIntensity{1.0f};
        bool renderSkybox{ false };
    };
}
