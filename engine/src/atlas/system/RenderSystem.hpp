#pragma once
#include <entt/entt.hpp>

#include "asset/AssetManager.hpp"
#include "renderer/Descriptors.hpp"
#include "renderer/Device.hpp"
#include "renderer/Pipeline.hpp"


namespace Atlas {
    /**
     * @deprecated - use RenderSystemV2 with culling
     */
    class RenderSystem {
    public:
        RenderSystem(Device &device, VkRenderPass renderPass, const DescriptorSetLayout& globalSetLayout);
        ~RenderSystem();

        RenderSystem(const RenderSystem&) = delete;
        RenderSystem &operator=(const RenderSystem&) = delete;

        void prepare(entt::registry &registry);

        void render(entt::registry &registry, VkCommandBuffer commandBuffer, VkDescriptorSet globalSet);

    private:
        void createDescriptors();
        void createPipelineLayout(const DescriptorSetLayout &globalSetLayout);
        void createPipeline(VkRenderPass renderPass);
        void commitSamplersToDescriptors();

        uint32_t registerTexture(AssetHandle handle);

        Device &device;

        std::unique_ptr<Pipeline> pipeline;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

        // Bindless texture descriptors
        std::unique_ptr<DescriptorSetLayout> textureSetLayout;
        std::unique_ptr<DescriptorPool> bindlessTexturePool;
        VkDescriptorSet bindlessTextureSet = VK_NULL_HANDLE;

        // Texture management - maps AssetHandle to GPU descriptor array index
        uint32_t nextTextureIndex = 1;
        std::vector<std::shared_ptr<Sampler>> waitingToBeCommitedSamplers;
        std::unordered_map<AssetHandle, uint32_t> handleToGPUIndex;
        AssetHandle defaultWhiteTextureHandle = INVALID_ASSET_HANDLE;  // Just store the handle
    };
}
