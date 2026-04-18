#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <entt/entity/registry.hpp>

#include "renderer/Camera.hpp"
#include "renderer/Renderer.hpp"
#include "renderer/RenderGraph.hpp"
#include "renderer/abstraction/Buffer.hpp"
#include "renderer/abstraction/Descriptors.hpp"


namespace Atlas {
    struct alignas(16) DebugData {
        float irradianceMultiplier{1.0f};
        float exposureMultiplier{1.0f};
        float _padding[2]{};
    };

    struct alignas(16) GlobalUbo {
        Camera::Data cameraData;
        DebugData debugData{};
    };

    class RenderSystemV2 {
    public:
        static constexpr uint32_t G_BUFFER_HEIGHT = 1920;
        static constexpr uint32_t G_BUFFER_WIDTH = 1080;

        RenderSystemV2(Device &device, Renderer &renderer);
        ~RenderSystemV2() = default;

        RenderSystemV2(const RenderSystemV2 &) = delete;
        RenderSystemV2 &operator=(const RenderSystemV2 &) = delete;

        void build(entt::registry &registry);
        void render(VkCommandBuffer graphicsCmdBuffer, uint32_t frameIndex, const GlobalUbo &globalUbo);

    private:
        void createGlobalUbo();

        Device &device;
        std::unique_ptr<RenderGraph> renderGraph;

        // Set 0 - Global descriptors (UBO)
        std::unique_ptr<DescriptorSetLayout> globalSetLayout;
        std::unique_ptr<DescriptorPool> globalPool;
        std::vector<std::unique_ptr<Buffer> > globalUboBuffers;
        std::vector<VkDescriptorSet> globalDescriptorSets;
    };
}
