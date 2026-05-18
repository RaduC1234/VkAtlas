#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <entt/entity/registry.hpp>

#include "renderer/Camera.hpp"
#include "renderer/Renderer.hpp"
#include "renderer/RenderGraph.hpp"
#include "renderer/abstraction/GPUBuffer.hpp"
#include "renderer/abstraction/Descriptors.hpp"


namespace Atlas {
    class AssetManager;
    struct alignas(16) DebugData {
        float irradianceMultiplier{1.0f};
        float exposureMultiplier{1.0f};
        ViewMode viewMode{ViewMode::LIT};
        float _padding{};
    };

    class RenderSystemV2 {
    public:
        static constexpr uint32_t G_BUFFER_HEIGHT = 1920;
        static constexpr uint32_t G_BUFFER_WIDTH = 1080;

        RenderSystemV2(Device &device, Renderer &renderer, AssetManager &assets);
        ~RenderSystemV2() = default;

        RenderSystemV2(const RenderSystemV2 &) = delete;
        RenderSystemV2 &operator=(const RenderSystemV2 &) = delete;

        void build(entt::registry &registry);
        void build(entt::registry &registry, ViewMode viewMode);
        void render(FrameContext frameContext, const Camera::Data &cameraData, const DebugData &debugData) const;

    private:
        struct alignas(16) GlobalUbo {
            Camera::Data cameraData{};
            DebugData debugData{};
        };

        void createGlobalUbo();
        void resetPathTracing();
        bool pathTracingCameraChanged(const Camera::Data &cameraData) const;

        Device &device;
        std::unordered_map<ViewMode, std::shared_ptr<RenderGraph> > renderGraphs;

        // Set 0 - Global descriptors (UBO)
        std::unique_ptr<DescriptorSetLayout> globalSetLayout;
        std::unique_ptr<DescriptorPool> globalPool;
        std::vector<std::unique_ptr<GPUBuffer> > globalUboBuffers;
        std::vector<VkDescriptorSet> globalDescriptorSets;
    };
}
