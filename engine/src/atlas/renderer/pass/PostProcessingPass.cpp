#include "PostProcessingPass.hpp"
#include <stdexcept>

#include "asset/AssetManager.hpp"

namespace Atlas {
    PostProcessPass::PostProcessPass(Device &device, VkRenderPass swapchainRenderPass, VkImageView geometryColorView, const DescriptorSetLayout &globalSetLayout): device(device) {
        createSampler();
        createDescriptors(geometryColorView);
        createPipelineLayout(globalSetLayout);
        createPipeline(swapchainRenderPass);
    }

    PostProcessPass::~PostProcessPass() {
        vkDestroyPipelineLayout(device.device(), pipelineLayout, nullptr);
        vkDestroySampler(device.device(), sampler, nullptr);
    }

    void PostProcessPass::createSampler() {
        VkSamplerCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        info.magFilter = VK_FILTER_LINEAR;
        info.minFilter = VK_FILTER_LINEAR;
        info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        info.minLod = 0.0f;
        info.maxLod = 0.0f;

        if (vkCreateSampler(device.device(), &info, nullptr, &sampler) != VK_SUCCESS)
            throw std::runtime_error("PostProcessPass: failed to create sampler");
    }

    void PostProcessPass::createDescriptors(VkImageView geometryColorView) {
        AssetHandle BRDFHandle = AssetManager::get().loadTexture(
            "engine/brdf_lut.hdr",
            VK_FORMAT_R32G32B32A32_SFLOAT,
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
        );

        auto BRDFTex = AssetManager::get().getTexture(BRDFHandle);
        if (!BRDFTex) {
            throw std::runtime_error("Failed to load BRDF LUT!");
        }

        // set 1 - hdrInput + BRDF LUT
        inputSetLayout = DescriptorSetLayout::Builder(device)
                .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1) // hdrInput
                .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1) // BRDF LUT
                .build();

        pool = DescriptorPool::Builder(device)
                .setMaxSets(1)
                .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2)
                .build();

        if (!pool->allocateDescriptor(inputSetLayout->getDescriptorSetLayout(), inputSet))
            throw std::runtime_error("PostProcessPass: failed to allocate descriptor set");

        VkDescriptorImageInfo hdrInfo{};
        hdrInfo.sampler = sampler;
        hdrInfo.imageView = geometryColorView;
        hdrInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorImageInfo brdfInfo{};
        brdfInfo.sampler = BRDFTex->getSampler();
        brdfInfo.imageView = BRDFTex->getImageView();
        brdfInfo.imageLayout = BRDFTex->getImageLayout();

        VkWriteDescriptorSet w0{};
        w0.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w0.dstSet = inputSet;
        w0.dstBinding = 0;
        w0.descriptorCount = 1;
        w0.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w0.pImageInfo = &hdrInfo;

        VkWriteDescriptorSet w1 = w0;
        w1.dstBinding = 1;
        w1.pImageInfo = &brdfInfo;

        const VkWriteDescriptorSet writes[] = {w0, w1};
        vkUpdateDescriptorSets(device.device(), 2, writes, 0, nullptr);
    }

    void PostProcessPass::createPipelineLayout(const DescriptorSetLayout &globalSetLayout) {
        const std::vector layouts = {
            globalSetLayout.getDescriptorSetLayout(),
            inputSetLayout->getDescriptorSetLayout(),
        };

        VkPipelineLayoutCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        info.setLayoutCount = static_cast<uint32_t>(layouts.size());
        info.pSetLayouts = layouts.data();

        if (vkCreatePipelineLayout(device.device(), &info, nullptr, &pipelineLayout) != VK_SUCCESS)
            throw std::runtime_error("PostProcessPass: failed to create pipeline layout");
    }

    void PostProcessPass::createPipeline(VkRenderPass swapChainRenderPass) {
        GraphicsPipelineConfigInfo cfg{};
        Pipeline::defaultGraphicsPipelineConfigInfo(cfg);

        cfg.bindingDescriptions = {};
        cfg.attributeDescriptions = {};
        cfg.depthStencilInfo.depthTestEnable = VK_FALSE;
        cfg.depthStencilInfo.depthWriteEnable = VK_FALSE;
        cfg.renderPass = swapChainRenderPass;
        cfg.pipelineLayout = pipelineLayout;

        pipeline = std::make_unique<Pipeline>(
            device,
            "shaders/PostProcess.vert.spv",
            "shaders/PostProcess.frag.spv",
            cfg
        );
    }

    void PostProcessPass::record(VkCommandBuffer cmd, VkDescriptorSet globalSet) {
        pipeline->bind(cmd);

        const VkDescriptorSet sets[] = { globalSet, inputSet };
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0,
                                static_cast<uint32_t>(std::size(sets)), sets, 0, nullptr);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }
} // Atlas
