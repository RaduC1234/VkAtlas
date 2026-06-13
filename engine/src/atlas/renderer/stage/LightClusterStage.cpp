#include "LightClusterStage.hpp"

#include <array>
#include <stdexcept>

#include "core/Profiler.hpp"

namespace Atlas {
    LightClusterStage::LightClusterStage(Device &device, const DescriptorSetLayout &globalSetLayout)
        : RenderStage(Queue::COMPUTE), device(device), globalSetLayout(globalSetLayout) {
        setLayout = DescriptorSetLayout::Builder(device)
                .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 1) // lights
                .addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 1) // clusters
                .build();

        pool = DescriptorPool::Builder(device)
                .setMaxSets(1)
                .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2)
                .build();

        if (!pool->allocateDescriptor(setLayout->getDescriptorSetLayout(), set)) {
            throw std::runtime_error("LightClusterStage: failed to allocate descriptor set");
        }
    }

    LightClusterStage::~LightClusterStage() {
        vkDestroyPipelineLayout(device.device(), pipelineLayout, nullptr);
    }

    void LightClusterStage::getDeclaredInputs(std::vector<std::string> &out) const {
        out.push_back("scene_lights");
    }

    void LightClusterStage::getDeclaredOutputs(std::vector<Resource::Description> &out) const {
        out.push_back(Resource::Description::storageBuffer("cluster_lights", CLUSTER_COUNT * CLUSTER_SIZE));
    }

    void LightClusterStage::onResourcesCreated(const Context &ctx) {
        auto lightInfo = ctx.resources.at("scene_lights").get().asBuffer().descriptorInfo();
        auto clusterInfo = ctx.resources.at("cluster_lights").get().asBuffer().descriptorInfo();

        DescriptorWriter(*setLayout, *pool)
                .writeBuffer(0, &lightInfo)
                .writeBuffer(1, &clusterInfo)
                .overwrite(set);

        createPipeline();
    }

    void LightClusterStage::createPipeline() {
        const std::array layouts = {
            globalSetLayout.getDescriptorSetLayout(),
            setLayout->getDescriptorSetLayout(),
        };

        VkPipelineLayoutCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        info.setLayoutCount = static_cast<uint32_t>(layouts.size());
        info.pSetLayouts = layouts.data();

        if (vkCreatePipelineLayout(device.device(), &info, nullptr, &pipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("LightClusterStage: failed to create pipeline layout");
        }

        ComputePipelineConfigInfo cfg{pipelineLayout};
        Pipeline::defaultComputePipelineConfigInfo(cfg);

        pipeline = std::make_unique<Pipeline>(device, "##engine/shaders/LightCluster.comp.spv", cfg);
    }

    void LightClusterStage::record(VkCommandBuffer cmd, VkDescriptorSet globalSet) {
        ATLAS_PROFILE_SCOPE("LightClusterStage::record");
        ATLAS_PROFILE_GPU_ZONE(device.gpuProfilerContext(), cmd, "LightClusterStage");

        // The graph only orders read-after-write; guard the previous frame's
        // fragment reads before overwriting the cluster lists (write-after-read).
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 0, nullptr);

        pipeline->bind(cmd);

        const VkDescriptorSet sets[] = {globalSet, set};
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout,
                                0, std::size(sets), sets, 0, nullptr);

        // One thread per cluster; local_size_x = 64 in LightCluster.comp.
        vkCmdDispatch(cmd, (CLUSTER_COUNT + 63) / 64, 1, 1);
    }
} // namespace Atlas
