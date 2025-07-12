#pragma once
#include <entt/entt.hpp>

#include "renderer/Pipeline.hpp"
#include "renderer/Renderer.hpp"

namespace Atlas {
    class RenderSystem {
    public:
        RenderSystem(Device &device, VkRenderPass renderPass);
        ~RenderSystem();

        RenderSystem(const RenderSystem&) = delete;
        RenderSystem &operator=(const RenderSystem&) = delete;

        void update(entt::registry &registry, VkCommandBuffer commandBuffer) const;
    private:
        void createPipelineLayout();
        void createPipeline(VkRenderPass renderPass);

        Device &device;

        std::unique_ptr<Pipeline> pipeline;
        VkPipelineLayout pipelineLayout;
    };
}
