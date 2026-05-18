#include "RenderSystemV2.hpp"

#include "core/Log.hpp"
#include "renderer/stage/GeometryStage.hpp"
#include "renderer/stage/OutputStage.hpp"
#include "renderer/stage/PathTracingStage.hpp"
#include "renderer/stage/PostProcessingStage.hpp"

namespace Atlas {
    RenderSystemV2::RenderSystemV2(Device &device, Renderer &renderer, AssetManager &assets) : device(device) {
        createGlobalUbo();

        auto rasterGraph = std::make_shared<RenderGraph>(RenderGraph::Builder(device)
            .addStage<GeometryStage>(device, assets, *globalSetLayout)
            .addStage<PostProcessPass>(device, *globalSetLayout)
            .addStage<OutputStage>(device, renderer)
            .setExtent(G_BUFFER_WIDTH, G_BUFFER_HEIGHT)
            .build(RenderGraph::Mode::MultiPass));

        auto rayTracingGraph = std::make_shared<RenderGraph>(RenderGraph::Builder(device)
            .addStage<PathTracingStage>(device, assets, *globalSetLayout)
            .addStage<OutputStage>(device, renderer)
            .setExtent(G_BUFFER_WIDTH, G_BUFFER_HEIGHT)
            .build(RenderGraph::Mode::MultiPass));

        renderGraphs[ViewMode::LIT] = renderGraphs[ViewMode::UNLIT] = renderGraphs[ViewMode::LIGHTING_ONLY] = rasterGraph;
        renderGraphs[ViewMode::PATH_TRACING] = std::move(rayTracingGraph);
    }


    void RenderSystemV2::build(entt::registry &registry) {
        build(registry, ViewMode::LIT);
    }

    void RenderSystemV2::build(entt::registry &registry, ViewMode viewMode) {
        renderGraphs.at(viewMode)->build(registry);
    }

    void RenderSystemV2::render(const FrameContext frameContext,const Camera::Data &cameraData,const DebugData &debugData) const {
        GlobalUbo globalUbo{};
        globalUbo.cameraData = cameraData;
        globalUbo.debugData = debugData;
        globalUboBuffers[frameContext.index]->uploadData(&globalUbo, sizeof(GlobalUbo));

        renderGraphs.at(debugData.viewMode)->render(frameContext, globalDescriptorSets[frameContext.index]);
    }

    void RenderSystemV2::createGlobalUbo() {
        globalSetLayout = DescriptorSetLayout::Builder(device)
                .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL)
                .build();

        globalPool = DescriptorPool::Builder(device)
                .setMaxSets(SwapChain::MAX_FRAMES_IN_FLIGHT)
                .addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, SwapChain::MAX_FRAMES_IN_FLIGHT)
                .build();

        globalUboBuffers.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
        globalDescriptorSets.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);

        for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; i++) {
            globalUboBuffers[i] = std::make_unique<GPUBuffer>(
                device,
                sizeof(GlobalUbo),
                1,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VMA_MEMORY_USAGE_AUTO,
                device.properties.limits.minUniformBufferOffsetAlignment,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                VMA_ALLOCATION_CREATE_MAPPED_BIT
            );
            globalUboBuffers[i]->map();

            auto bufferInfo = globalUboBuffers[i]->descriptorInfo();
            DescriptorWriter(*globalSetLayout, *globalPool)
                    .writeBuffer(0, &bufferInfo)
                    .build(globalDescriptorSets[i]);
        }
    }
} // namespace Atlas
