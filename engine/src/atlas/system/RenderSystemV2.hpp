#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include "asset/AssetManager.hpp"
#include "entity/Object.hpp"
#include "renderer/Device.hpp"
#include "renderer/Renderer.hpp"
#include "renderer/RenderGraph.hpp"
#include "renderer/abstraction/Descriptors.hpp"
#include "renderer/stage/GeometryPass.hpp"
#include "renderer/stage/PostProcessingPass.hpp"

namespace Atlas {
    struct alignas(16) GlobalUbo {
        Camera::Data cameraData;
        glm::vec4 ambientColor{1.0f, 1.0f, 1.0f, 0.002f};
    };

    class RenderSystemV2 {
    public:
        static constexpr uint32_t G_BUFFER_HEIGHT = 1920;
        static constexpr uint32_t G_BUFFER_WIDTH = 1080;

        RenderSystemV2(Device& device, Renderer &renderer);
        ~RenderSystemV2() = default;

        RenderSystemV2(const RenderSystemV2 &) = delete;
        RenderSystemV2 &operator=(const RenderSystemV2 &) = delete;

        void build(entt::registry &registry);
        void render(VkCommandBuffer graphicsCmdBuffer, uint32_t frameIndex, const GlobalUbo &globalUbo);

    private:
        void createGlobalUbo();

        Device& device;
        std::unique_ptr<RenderGraph> renderGraph;

        // Set 0 - Global descriptors (UBO)
        std::unique_ptr<DescriptorSetLayout> globalSetLayout;
        std::unique_ptr<DescriptorPool> globalPool;
        std::vector<std::unique_ptr<Buffer> > globalUboBuffers;
        std::vector<VkDescriptorSet> globalDescriptorSets;
    };
}
