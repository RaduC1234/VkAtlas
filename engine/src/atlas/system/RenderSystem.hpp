#pragma once
#include <entt/entt.hpp>

#include "renderer/Pipeline.hpp"
#include "renderer/Descriptors.hpp"

namespace Atlas {
    class RenderSystem {
    public:
        RenderSystem(Device &device, VkRenderPass renderPass, VkDescriptorSetLayout globalSetLayout);
        ~RenderSystem();

        RenderSystem(const RenderSystem&) = delete;
        RenderSystem &operator=(const RenderSystem&) = delete;

        void update(entt::registry &registry, VkCommandBuffer commandBuffer, VkDescriptorSet globalDescriptorSet) const;

        DescriptorSetLayout& getTextureSetLayout() const { return *textureSetLayout; }

    private:
        void createPipelineLayout(VkDescriptorSetLayout globalSetLayout);
        void createPipeline(VkRenderPass renderPass);

        Device &device;

        std::unique_ptr<Pipeline> pipeline;
        VkPipelineLayout pipelineLayout;
        std::unique_ptr<DescriptorSetLayout> textureSetLayout;
    };
}
