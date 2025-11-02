#pragma once
#include <entt/entt.hpp>

#include "renderer/Pipeline.hpp"
#include "renderer/Descriptors.hpp"
#include "renderer/Sampler.hpp"
#include "renderer/Buffer.hpp"

#include <glm/glm.hpp>

namespace Atlas {
    class RenderSystem {
    public:
        RenderSystem(Device &device, VkRenderPass renderPass);
        ~RenderSystem();

        RenderSystem(const RenderSystem&) = delete;
        RenderSystem &operator=(const RenderSystem&) = delete;

        void registerMaterials(entt::registry &registry);

        uint32_t registerTexture(std::shared_ptr<Sampler> texture);

        // Update UBO data per frame
        void updateUBO(int frameIndex, const glm::mat4& projection, const glm::mat4& view,
                       const glm::vec4& ambientColor, const glm::vec3& lightPosition, const glm::vec4& lightColor);

        // Render all objects
        void render(entt::registry &registry, VkCommandBuffer commandBuffer, int frameIndex);

    private:
        void createDescriptors();
        void createPipelineLayout();
        void createPipeline(VkRenderPass renderPass);
        void createSamplersBuffer();
        void commitSamplersToDescriptors();

        Device &device;

        std::unique_ptr<Pipeline> pipeline;
        VkPipelineLayout pipelineLayout;

        // Global descriptors (UBO)
        std::unique_ptr<DescriptorSetLayout> globalSetLayout;
        std::unique_ptr<DescriptorPool> globalPool;
        std::vector<std::unique_ptr<Buffer>> uboBuffers;
        std::vector<VkDescriptorSet> globalDescriptorSets;

        // Bindless texture descriptors
        std::unique_ptr<DescriptorSetLayout> textureSetLayout;
        std::unique_ptr<DescriptorPool> bindlessTexturePool;
        VkDescriptorSet bindlessTextureSet = VK_NULL_HANDLE;

        // Texture management
        std::vector<std::shared_ptr<Sampler>> registeredTextures;
        uint32_t nextTextureIndex = 0;
        std::shared_ptr<Sampler> defaultTexture;

        std::unordered_map<std::string, uint32_t> samplersIndexMap;
    };
}
