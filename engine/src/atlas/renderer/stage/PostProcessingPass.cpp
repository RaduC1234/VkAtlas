#include "PostProcessingPass.hpp"
#include <stdexcept>

#include "asset/AssetManager.hpp"

namespace Atlas {
    PostProcessPass::PostProcessPass(Device &device, const DescriptorSetLayout &globalSetLayout) : IRenderStage(Queue::GRAPHICS), device(device), globalSetLayout(globalSetLayout) {
        createSampler();
    }

    PostProcessPass::~PostProcessPass() {
        vkDestroyFramebuffer(device.device(), framebuffer, nullptr);
        vkDestroyRenderPass(device.device(), renderPass, nullptr);
        vkDestroyPipelineLayout(device.device(), pipelineLayout, nullptr);
        vkDestroySampler(device.device(), stencilSampler, nullptr);
        vkDestroySampler(device.device(), colorSampler, nullptr);
    }

    void PostProcessPass::getDeclaredOutputs(std::vector<Resource::Description> &out) const {
        out.push_back(Resource::Description::color("post_color", VK_FORMAT_R8G8B8A8_SRGB));
    }

    void PostProcessPass::getDeclaredInputs(std::vector<std::string> &out) const {
        out.push_back("geometry_color");
        out.push_back("geometry_depth");
    }

    void PostProcessPass::onResourcesCreated(
        const std::unordered_map<std::string, std::reference_wrapper<Resource> > &resources) {
        const GPUImage &colorImage = resources.at("geometry_color").get().asGPUImage();
        const GPUImage &depthImage = resources.at("geometry_depth").get().asGPUImage();
        const GPUImage &outImage = resources.at("post_color").get().asGPUImage();

        postColorTarget = &outImage;
        extent = outImage.extent();

        createDescriptors(colorImage, depthImage);
        createPipelineLayout();
        createRenderPass(outImage.format());
        createFramebuffer(outImage);
        createPipeline();
    }

    // -------------------------------------------------------------------------
    // Samplers
    // -------------------------------------------------------------------------

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

        if (vkCreateSampler(device.device(), &info, nullptr, &colorSampler) != VK_SUCCESS)
            throw std::runtime_error("PostProcessPass: failed to create colorSampler");

        VkSamplerCreateInfo stencilInfo{};
        stencilInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        stencilInfo.magFilter = VK_FILTER_NEAREST;
        stencilInfo.minFilter = VK_FILTER_NEAREST;
        stencilInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        stencilInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        stencilInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

        if (vkCreateSampler(device.device(), &stencilInfo, nullptr, &stencilSampler) != VK_SUCCESS)
            throw std::runtime_error("PostProcessPass: failed to create stencilSampler");
    }

    // -------------------------------------------------------------------------
    // Render pass — single LDR colour attachment, no depth.
    // finalLayout = SHADER_READ_ONLY so RenderGraph layout tracking matches
    // writeLayoutFor(ATTACHMENT_COLOR) and no extra barrier is inserted before
    // OutputPass reads it.
    // -------------------------------------------------------------------------

    void PostProcessPass::createRenderPass(VkFormat colorFmt) {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = colorFmt;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // full-screen draw overwrites everything
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        // Transition directly to SHADER_READ_ONLY so OutputPass can blit without
        // an intermediate layout barrier from the RenderGraph.
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference colorRef{};
        colorRef.attachment = 0;
        colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;

        // Subpass dependency: ensure geometry writes are visible to the fragment
        // shader that samples geometry_color/depth.
        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                           VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dep.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo rpInfo{};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = 1;
        rpInfo.pAttachments = &colorAttachment;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies = &dep;

        if (vkCreateRenderPass(device.device(), &rpInfo, nullptr, &renderPass) != VK_SUCCESS)
            throw std::runtime_error("PostProcessPass: failed to create render pass");
    }

    void PostProcessPass::createFramebuffer(const GPUImage &colorImage) {
        VkImageView view = colorImage.view(0); // view 0 = COLOR aspect

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = renderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &view;
        fbInfo.width = extent.width;
        fbInfo.height = extent.height;
        fbInfo.layers = 1;

        if (vkCreateFramebuffer(device.device(), &fbInfo, nullptr, &framebuffer) != VK_SUCCESS) {
            throw std::runtime_error("PostProcessPass: failed to create framebuffer");
        }
    }

    void PostProcessPass::createDescriptors(const GPUImage &colorImage, const GPUImage &depthImage) {
        inputSetLayout = DescriptorSetLayout::Builder(device)
                .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1) // hdrInput {z
                .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1) // stencil
                .build();

        pool = DescriptorPool::Builder(device)
                .setMaxSets(1)
                .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2)
                .build();

        if (!pool->allocateDescriptor(inputSetLayout->getDescriptorSetLayout(), inputSet))
            throw std::runtime_error("PostProcessPass: failed to allocate descriptor set");

        VkDescriptorImageInfo hdrInfo{};
        hdrInfo.sampler = colorSampler;
        hdrInfo.imageView = colorImage.view(0);
        hdrInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorImageInfo stencilInfo{};
        stencilInfo.sampler = stencilSampler;
        stencilInfo.imageView = depthImage.view(1); // stencil aspect view
        stencilInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet w0{};
        w0.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w0.dstSet = inputSet;
        w0.dstBinding = 0;
        w0.descriptorCount = 1;
        w0.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w0.pImageInfo = &hdrInfo;

        VkWriteDescriptorSet w1 = w0;
        w1.dstBinding = 1;
        w1.pImageInfo = &stencilInfo;

        const VkWriteDescriptorSet writes[] = {w0, w1};
        vkUpdateDescriptorSets(device.device(), std::size(writes), writes, 0, nullptr);
    }

    void PostProcessPass::createPipelineLayout() {
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

    void PostProcessPass::createPipeline() {
        GraphicsPipelineConfigInfo cfg{};
        Pipeline::defaultGraphicsPipelineConfigInfo(cfg);

        cfg.bindingDescriptions = {};
        cfg.attributeDescriptions = {};
        cfg.depthStencilInfo.depthTestEnable = VK_FALSE;
        cfg.depthStencilInfo.depthWriteEnable = VK_FALSE;
        cfg.renderPass = renderPass; // our offscreen pass now
        cfg.pipelineLayout = pipelineLayout;

        pipeline = std::make_unique<Pipeline>(
            device,
            "shaders/PostProcess.vert.spv",
            "shaders/PostProcess.frag.spv",
            cfg
        );
    }

    void PostProcessPass::record(VkCommandBuffer cmd, VkDescriptorSet globalSet) {
        VkRenderPassBeginInfo rpInfo{};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpInfo.renderPass = renderPass;
        rpInfo.framebuffer = framebuffer;
        rpInfo.renderArea.offset = {0, 0};
        rpInfo.renderArea.extent = extent;

        vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.width = static_cast<float>(extent.width);
        viewport.height = static_cast<float>(extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        VkRect2D scissor{{0, 0}, extent};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        pipeline->bind(cmd);

        const VkDescriptorSet sets[] = {globalSet, inputSet};
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, std::size(sets), sets, 0, nullptr);

        vkCmdDraw(cmd, 3, 1, 0, 0); // full-screen triangle

        vkCmdEndRenderPass(cmd);
    }
} // namespace Atlas
