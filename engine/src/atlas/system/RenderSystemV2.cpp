#include "RenderSystemV2.hpp"

#include <renderer/ImGuiLayer.hpp>

#include "core/Log.hpp"
#include "renderer/SwapChain.hpp"

namespace Atlas {
    RenderSystemV2::RenderSystemV2(Device& device, VkRenderPass swapChainRenderPass) : device(device) {
        createGlobalUbo();

        geometryPass = std::make_unique<GeometryPass>(device, G_BUFFER_WIDTH, G_BUFFER_HEIGHT, *globalSetLayout);
        postProcessPass = std::make_unique<PostProcessPass>(device, swapChainRenderPass, geometryPass->getColorView(), *globalSetLayout);
    }

    void RenderSystemV2::build(entt::registry &registry) {
        geometryPass->build(registry);
    }

    void RenderSystemV2::render(Renderer& renderer, const GlobalUbo &globalUbo, ImGuiLayer& imGui) {
        uint32_t frameIndex = renderer.getFrameIndex();

        if (VkCommandBuffer graphicsCommandBuffer = renderer.getCurrentGraphicsCommandBuffer()) {
            globalUboBuffers[frameIndex]->uploadData(&globalUbo, sizeof(GlobalUbo));

            // pass 1
            geometryPass->begin(graphicsCommandBuffer);
            geometryPass->record(graphicsCommandBuffer, globalDescriptorSets[frameIndex]);
            geometryPass->end(graphicsCommandBuffer);
            geometryPass->barrier(graphicsCommandBuffer);

            // pass 2
            renderer.beginSwapChainRenderPass(graphicsCommandBuffer);
            postProcessPass->record(graphicsCommandBuffer, globalDescriptorSets[frameIndex]);
            imGui.endFrame(graphicsCommandBuffer);
            renderer.endSwapChainRenderPass(graphicsCommandBuffer);
        } else {
            AT_FATAL("Could not get current graphics command buffer.");
        }
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
