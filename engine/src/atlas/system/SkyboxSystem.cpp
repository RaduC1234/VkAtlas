#include "SkyboxSystem.hpp"

#include <stdexcept>

#include "core/Log.hpp"
#include "entity/Object.hpp"

namespace Atlas {
    static constexpr uint32_t CUBE_VERTEX_COUNT = 36;

    SkyboxSystem::SkyboxSystem(Device &device, VkRenderPass renderPass, const DescriptorSetLayout &globalSetLayout): device(device) {
        createDescriptors();
        createPipelineLayout(globalSetLayout);
        createPipeline(renderPass);
    }

    SkyboxSystem::~SkyboxSystem() {
        vkDestroyPipelineLayout(device.device(), pipelineLayout, nullptr);
    }

    void SkyboxSystem::createDescriptors() {
        samplerCubeSetLayout = DescriptorSetLayout::Builder(device)
                .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
                .build();

        samplerCubePool = DescriptorPool::Builder(device)
                .setMaxSets(1)
                .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1)
                .build();

        if (!samplerCubePool->allocateDescriptor(samplerCubeSetLayout->getDescriptorSetLayout(), samplerSet)) {
            throw std::runtime_error("Failed to allocate skybox descriptor set");
        }
    }

    void SkyboxSystem::createPipelineLayout(const DescriptorSetLayout &globalSetLayout) {
        std::vector<VkDescriptorSetLayout> descriptorSetLayouts{
            globalSetLayout.getDescriptorSetLayout(),
            samplerCubeSetLayout->getDescriptorSetLayout()
        };

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
        pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
        pipelineLayoutInfo.pushConstantRangeCount = 0;
        pipelineLayoutInfo.pPushConstantRanges = nullptr;

        if (vkCreatePipelineLayout(device.device(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create pipeline layout!");
        }
    }

    void SkyboxSystem::createPipeline(VkRenderPass renderPass) {
        assert(pipelineLayout != VK_NULL_HANDLE && "Cannot create pipeline before pipeline layout");

        PipelineConfigInfo pipelineConfig{};
        Pipeline::defaultPipelineConfigInfo(pipelineConfig);

        pipelineConfig.depthStencilInfo.depthWriteEnable = VK_FALSE;
        pipelineConfig.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        pipelineConfig.rasterizationInfo.cullMode = VK_CULL_MODE_BACK_BIT;

        pipelineConfig.renderPass = renderPass;
        pipelineConfig.pipelineLayout = pipelineLayout;

        pipeline = std::make_unique<Pipeline>(
            device,
            "shaders/skybox.vert.spv",
            "shaders/skybox.frag.spv",
            pipelineConfig
        );
    }

    void SkyboxSystem::render(entt::registry &registry, VkCommandBuffer commandBuffer, VkDescriptorSet globalSet) {
        // Find skybox entity
        auto view = registry.view<SkyboxComponent>();

        AssetHandle cubemapHandle = INVALID_ASSET_HANDLE;
        for (auto entity: view) {
            auto &skybox = view.get<SkyboxComponent>(entity);
            cubemapHandle = skybox.cubemapHandle;
            break; // Only use first skybox
        }

        if (cubemapHandle == INVALID_ASSET_HANDLE) {
            AT_WARN("No skybox entity found in registry");
            return;
        }

        // if cubemap changed
        if (cubemapHandle != boundCubemapHandle) {
            auto cubemap = AssetManager::get().getCubemap(cubemapHandle);
            if (!cubemap)
                return;

            VkDescriptorImageInfo imageInfo = cubemap->descriptorInfo();

            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = samplerSet;
            write.dstBinding = 0;
            write.dstArrayElement = 0;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.descriptorCount = 1;
            write.pImageInfo = &imageInfo;

            vkUpdateDescriptorSets(device.device(), 1, &write, 0, nullptr);
            boundCubemapHandle = cubemapHandle;
        }

        pipeline->bind(commandBuffer);

        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &globalSet, 0, nullptr);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 1, 1, &samplerSet, 0, nullptr);

        vkCmdDraw(commandBuffer, CUBE_VERTEX_COUNT, 1, 0, 0);
    }
}
