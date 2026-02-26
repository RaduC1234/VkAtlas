#pragma once

#include "renderer/Device.hpp"
#include "renderer/Pipeline.hpp"

#include <entt/entt.hpp>

#include "asset/AssetManager.hpp"
#include "renderer/Descriptors.hpp"
#include "renderer/Sampler.hpp"

namespace Atlas {
    class EditorUISystem {
    public:
        EditorUISystem(Device& device, VkRenderPass renderPass, VkDescriptorSetLayout globalSetLayout);

        void render(entt::registry &registry, VkCommandBuffer commandBuffer, VkDescriptorSet globalDescriptorSet) const;

    private:
        constexpr static uint32_t MAX_TEXTURES = 128;

        struct alignas(16) BillboardPushConstant;

        void createDescriptors();
        void createPipelineLayout(VkDescriptorSetLayout GlobalSetLayout);
        void createPipeline(VkRenderPass RenderPass);
        void uploadTextures();

        Device &device;
        std::unique_ptr<Pipeline> pipeline;
        VkPipelineLayout pipelineLayout;

        // Bindless texture descriptors
        std::unique_ptr<DescriptorSetLayout> textureSetLayout;
        std::unique_ptr<DescriptorPool> texturesPool;
        VkDescriptorSet textureSet = VK_NULL_HANDLE;
    };
}
