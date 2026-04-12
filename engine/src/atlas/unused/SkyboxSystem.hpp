#pragma once
#include "asset/AssetManager.hpp"
#include "../renderer/abstraction/Buffer.hpp"
#include "renderer/Camera.hpp"
#include "../renderer/abstraction/Descriptors.hpp"
#include "renderer/Device.hpp"
#include "../renderer/abstraction/Pipeline.hpp"

namespace Atlas {
    class SkyboxSystem {
    public:
        SkyboxSystem(Device& device, VkRenderPass renderPass, const DescriptorSetLayout& globalSetLayout);
        ~SkyboxSystem();

        SkyboxSystem(const SkyboxSystem&) = delete;
        SkyboxSystem &operator=(const SkyboxSystem&) = delete;

        void render(entt::registry &registry, VkCommandBuffer commandBuffer, VkDescriptorSet globalSet);
    private:
        Device& device;
        std::unique_ptr<Pipeline> pipeline;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

        std::unique_ptr<DescriptorSetLayout> samplerCubeSetLayout;
        std::unique_ptr<DescriptorPool> samplerCubePool;
        VkDescriptorSet samplerSet{VK_NULL_HANDLE};

        void createDescriptors();
        void createPipelineLayout(const DescriptorSetLayout &globalSetLayout);
        void createPipeline(VkRenderPass renderPass);

        AssetHandle boundCubemapHandle = INVALID_ASSET_HANDLE;
    };
}
