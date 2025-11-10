#pragma once
#include <entt/entt.hpp>

#include "renderer/Pipeline.hpp"
#include "renderer/Descriptors.hpp"
#include "renderer/Sampler.hpp"
#include "renderer/Buffer.hpp"
#include "asset/AssetManager.hpp"

#include <glm/glm.hpp>

namespace Atlas {
    class RenderSystem {
    public:
        RenderSystem(Device &device, VkRenderPass renderPass);
        ~RenderSystem();

        RenderSystem(const RenderSystem&) = delete;
        RenderSystem &operator=(const RenderSystem&) = delete;

        uint32_t registerTexture(AssetHandle handle);

        void prepareTextures(entt::registry &registry);

        void updateUBO(int frameIndex, const glm::mat4& projection, const glm::mat4& view,
                       const glm::vec4& ambientColor, const glm::vec3& lightPosition, const glm::vec4& lightColor);

        void render(entt::registry &registry, VkCommandBuffer commandBuffer, int frameIndex);

    private:
        void createDescriptors();
        void createPipelineLayout();
        void createPipeline(VkRenderPass renderPass);
        void commitSamplersToDescriptors();

        Device &device;

        std::unique_ptr<Pipeline> pipeline;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

        // Global descriptors (UBO)
        std::unique_ptr<DescriptorSetLayout> globalSetLayout;
        std::unique_ptr<DescriptorPool> globalPool;
        std::vector<std::unique_ptr<Buffer>> uboBuffers;
        std::vector<VkDescriptorSet> globalDescriptorSets;

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
