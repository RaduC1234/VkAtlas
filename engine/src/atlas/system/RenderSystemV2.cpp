#include "RenderSystemV2.hpp"

#include <cmath>

#include "core/Log.hpp"
#include "renderer/stage/GeometryStage.hpp"
#include "renderer/stage/OutputStage.hpp"
#include "renderer/stage/PathTracingStage.hpp"
#include "renderer/stage/PostProcessingStage.hpp"

namespace Atlas {
    namespace {
        ViewMode resolveViewMode(const GlobalUbo &globalUbo) {
            const auto mode = static_cast<ViewMode>(globalUbo.debugData.viewMode);
            switch (mode) {
                case ViewMode::LIT:
                case ViewMode::UNLIT:
                case ViewMode::LIGHTING_ONLY:
                case ViewMode::PATH_TRACING:
                    return mode;
                default:
                    return ViewMode::LIT;
            }
        }

        bool matricesDiffer(const glm::mat4 &a, const glm::mat4 &b, float epsilon = 0.0001f) {
            for (int c = 0; c < 4; ++c) {
                for (int r = 0; r < 4; ++r) {
                    if (std::abs(a[c][r] - b[c][r]) > epsilon) {
                        return true;
                    }
                }
            }
            return false;
        }
    }

    RenderSystemV2::RenderSystemV2(Device &device, Renderer &renderer) : device(device) {
        createGlobalUbo();

        rasterGraph = std::make_unique<RenderGraph>(RenderGraph::Builder(device)
            .addStage<GeometryStage>(device, *globalSetLayout)
            .addStage<PostProcessPass>(device, *globalSetLayout)
            .addStage<OutputStage>(device, renderer)
            .setExtent(G_BUFFER_WIDTH, G_BUFFER_HEIGHT)
            .build(RenderGraph::Mode::MultiPass));

        auto pathStage = std::make_unique<PathTracingStage>(device, *globalSetLayout);
        pathTracingStage = pathStage.get();

        pathTracingGraph = std::make_unique<RenderGraph>(RenderGraph::Builder(device)
            .addStage(std::move(pathStage))
            .addStage<OutputStage>(device, renderer)
            .setExtent(G_BUFFER_WIDTH, G_BUFFER_HEIGHT)
            .build(RenderGraph::Mode::MultiPass));
    }

    void RenderSystemV2::build(entt::registry &registry) {
        rasterGraph->build(registry);
        pathTracingGraph->build(registry);
    }

    void RenderSystemV2::render(const FrameContext frameContext, const GlobalUbo &globalUbo) {
        const ViewMode viewMode = resolveViewMode(globalUbo);

        if (viewMode == ViewMode::PATH_TRACING) {
            if (activeViewMode != ViewMode::PATH_TRACING || pathTracingCameraChanged(globalUbo.cameraData)) {
                resetPathTracing();
            }
            lastPathTracingCamera = globalUbo.cameraData;
            hasLastPathTracingCamera = true;
        }

        activeViewMode = viewMode;
        globalUboBuffers[frameContext.index]->uploadData(&globalUbo, sizeof(GlobalUbo));

        RenderGraph &graph = viewMode == ViewMode::PATH_TRACING
                                 ? *pathTracingGraph
                                 : *rasterGraph;

        graph.render(frameContext, globalDescriptorSets[frameContext.index]);
    }

    void RenderSystemV2::resetPathTracing() {
        if (pathTracingStage) {
            pathTracingStage->reset();
        }
        hasLastPathTracingCamera = false;
    }

    bool RenderSystemV2::pathTracingCameraChanged(const Camera::Data &cameraData) const {
        if (!hasLastPathTracingCamera) {
            return true;
        }

        return matricesDiffer(cameraData.view, lastPathTracingCamera.view) ||
               matricesDiffer(cameraData.projection, lastPathTracingCamera.projection);
    }

    void RenderSystemV2::createGlobalUbo() {
        globalSetLayout = DescriptorSetLayout::Builder(device)
                .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                            VK_SHADER_STAGE_VERTEX_BIT
                            | VK_SHADER_STAGE_FRAGMENT_BIT
                            | VK_SHADER_STAGE_RAYGEN_BIT_KHR
                            | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
                            | VK_SHADER_STAGE_MISS_BIT_KHR)
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
