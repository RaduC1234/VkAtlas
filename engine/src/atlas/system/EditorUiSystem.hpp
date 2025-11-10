#pragma once

#include "renderer/Device.hpp"
#include "renderer/Pipeline.hpp"

#include <entt/entt.hpp>

namespace Atlas {
    class EditorUiSystem {
    public:
        EditorUiSystem(Device& device, VkRenderPass renderPass, VkDescriptorSetLayout globalSetLayout);

        void createPipelineLayout(VkDescriptorSetLayout GlobalSetLayout);
        void createPipeline(VkRenderPass RenderPass);

        void render(entt::registry &registry, VkCommandBuffer commandBuffer, const std::vector<VkDescriptorSet> &globalDescriptorSets, int frameIndex) const;
    private:
        Device &device;
        std::unique_ptr<Pipeline> pipeline;
        VkPipelineLayout pipelineLayout;
    };
}
