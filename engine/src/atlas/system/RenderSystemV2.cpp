#include "RenderSystemV2.hpp"

#include "core/Log.hpp"
#include "renderer/stage/GeometryPass.hpp"
#include "renderer/stage/OutputPass.hpp"
#include "renderer/stage/PostProcessingPass.hpp"

namespace Atlas {
    RenderSystemV2::RenderSystemV2(Device &device, Renderer &renderer) : device(device) {
        createGlobalUbo();

        this->renderGraph = RenderGraph::Builder(device)
                .addStage<GeometryPass>(device, *globalSetLayout)
                .addStage<PostProcessPass>(device, *globalSetLayout)
                .addStage<OutputPass>(device, renderer)
                .setExtent(G_BUFFER_WIDTH, G_BUFFER_HEIGHT)
                .build(RenderGraph::Mode::MultiPass);
    }

    void RenderSystemV2::build(entt::registry &registry) {
        renderGraph->build(registry);
    }

    void RenderSystemV2::render(VkCommandBuffer graphicsCmdBuffer, uint32_t frameIndex, const GlobalUbo &globalUbo) {
        globalUboBuffers[frameIndex]->uploadData(&globalUbo, sizeof(GlobalUbo));

        renderGraph->render(graphicsCmdBuffer, globalDescriptorSets[frameIndex]);
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
            globalUboBuffers[i] = std::make_unique<Buffer>(
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
