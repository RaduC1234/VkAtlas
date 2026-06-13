#pragma once

#include <memory>
#include <string>
#include <vector>

#include "RenderStage.hpp"
#include "renderer/Device.hpp"
#include "renderer/abstraction/Descriptors.hpp"
#include "renderer/abstraction/Pipeline.hpp"

namespace Atlas {
    // Bins every positional light into the view-space froxels it can reach so the
    // geometry pass only shades the lights overlapping a fragment's cluster.
    // Directional lights are sorted to the front of the light buffer by the
    // CullingStage and bypass the clusters entirely.
    class LightClusterStage : public RenderStage {
    public:
        // Grid and slice constants are mirrored in LightCluster.comp and Geometry.frag.
        static constexpr uint32_t GRID_X = 16;
        static constexpr uint32_t GRID_Y = 9;
        static constexpr uint32_t GRID_Z = 24;
        static constexpr uint32_t CLUSTER_COUNT = GRID_X * GRID_Y * GRID_Z;
        static constexpr uint32_t MAX_LIGHTS_PER_CLUSTER = 63;

        LightClusterStage(Device &device, const DescriptorSetLayout &globalSetLayout);
        ~LightClusterStage() override;

        LightClusterStage(const LightClusterStage &) = delete;
        LightClusterStage &operator=(const LightClusterStage &) = delete;

        void getDeclaredOutputs(std::vector<Resource::Description> &out) const override;
        void getDeclaredInputs(std::vector<std::string> &out) const override;

        void onResourcesCreated(const Context &ctx) override;

        void record(VkCommandBuffer cmd, VkDescriptorSet globalSet) override;

    private:
        // GPU layout per cluster: { uint count; uint indices[MAX_LIGHTS_PER_CLUSTER]; }
        static constexpr VkDeviceSize CLUSTER_SIZE = (1 + MAX_LIGHTS_PER_CLUSTER) * sizeof(uint32_t);

        void createPipeline();

        Device &device;
        const DescriptorSetLayout &globalSetLayout;

        std::unique_ptr<DescriptorPool> pool;
        std::unique_ptr<DescriptorSetLayout> setLayout;
        VkDescriptorSet set = VK_NULL_HANDLE;

        std::unique_ptr<Pipeline> pipeline;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    };
} // namespace Atlas
