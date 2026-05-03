#include "RenderSystemV2.hpp"

#include "core/Log.hpp"
#include "renderer/stage/GeometryStage.hpp"
#include "renderer/stage/OutputStage.hpp"
#include "renderer/stage/PathTracingStage.hpp"
#include "renderer/stage/PostProcessingStage.hpp"

namespace Atlas {
    RenderSystemV2::RenderSystemV2(Device &device, Renderer &renderer) : device(device) {
        createGlobalUbo();

        this->renderGraph = std::make_unique<RenderGraph>(RenderGraph::Builder(device)
               // .addStage<GeometryStage>(device, *globalSetLayout)
                //.addStage<PostProcessPass>(device, *globalSetLayout)
                .addStage<PathTracingStage>(device, *globalSetLayout)
                .addStage<OutputStage>(device, renderer)
                .setExtent(G_BUFFER_WIDTH, G_BUFFER_HEIGHT)
                .build(RenderGraph::Mode::MultiPass));
    }

    void RenderSystemV2::build(entt::registry &registry) {
        renderGraph->build(registry);
    }

    void RenderSystemV2::render(const FrameContext frameContext, const GlobalUbo &globalUbo) {
        globalUboBuffers[frameContext.index]->uploadData(&globalUbo, sizeof(GlobalUbo));

        renderGraph->render(frameContext, globalDescriptorSets[frameContext.index]);
    }

    void RenderSystemV2::createGlobalUbo() {
        globalSetLayout = DescriptorSetLayout::Builder(device)
                .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
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
