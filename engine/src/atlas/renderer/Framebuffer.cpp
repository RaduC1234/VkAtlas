/*
#include "Framebuffer.hpp"

namespace Atlas {
    Framebuffer::Framebuffer(Device &device, VkExtent2D extent, std::vector<AttachmentSpecification> attachments) : device(device), extent(extent), attachmentsSpecs(std::move(attachments)) {
        createAttachments();
        createRenderPass();
        createFramebuffer();
    }

    Framebuffer::~Framebuffer() {
        for (auto &attachment: attachments) {
            vkDestroyImageView(device.device(), attachment.imageView, nullptr);
            vmaDestroyImage(device.allocator(), attachment.image, attachment.allocation);
        }
        vkDestroyFramebuffer(device.device(), framebuffer, nullptr);
        vkDestroyRenderPass(device.device(), renderPass, nullptr);
    }

    VkDescriptorImageInfo Framebuffer::getDescriptorInfo(uint32_t index, VkSampler sampler) const {
        VkDescriptorImageInfo info{};
        info.sampler = sampler;
        info.imageView = attachments[index].imageView;
        info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        return info;
    }

    void Framebuffer::createAttachments() {
        attachments.resize(attachmentsSpecs.size());

        for (size_t i = 0; i < attachmentsSpecs.size(); i++) {
            const auto &spec = attachmentsSpecs[i];

            VkImageUsageFlags usage;
            switch (spec.type) {
                case AttachmentType::Color:
                    usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
                    break;
                case AttachmentType::Depth:
                    usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
                    break;
                default:
                    throw std::runtime_error("Invalid attachment type");
            }

            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.extent.width = extent.width;
            imageInfo.extent.height = extent.height;
            imageInfo.extent.depth = 1;
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.format = spec.format;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.usage = usage;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VmaAllocationCreateInfo allocCreateInfo{};
            allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

            if (vmaCreateImage(device.allocator(), &imageInfo, &allocCreateInfo, &attachments[i].image, &attachments[i].allocation, nullptr) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create framebuffer attachment image");
            }

            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = attachments[i].image;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = spec.format;

            if (spec.type == AttachmentType::Color) {
                viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            } else if (spec.type == AttachmentType::Depth) {
                viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            }

            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;

            if (vkCreateImageView(device.device(), &viewInfo, nullptr, &attachments[i].imageView) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create framebuffer attachment image view");
            }
        }

        void Framebuffer::createRenderPass() {
            std::vector<VkAttachmentDescription> attachmentDescriptions;
            std::vector<VkAttachmentReference> colorRefs;
            VkAttachmentReference depthRef{};
            bool hasDepth = false;

            for (size_t i = 0; i < attachmentsSpecs.size(); i++) {
                bool isDepth = attachmentsSpecs[i].type == AttachmentType::Depth;

                VkAttachmentDescription desc{};
                desc.format = attachmentsSpecs[i].format;
                desc.samples = VK_SAMPLE_COUNT_1_BIT;
                desc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                desc.storeOp = isDepth
                                   ? VK_ATTACHMENT_STORE_OP_DONT_CARE
                                   : VK_ATTACHMENT_STORE_OP_STORE;
                desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                desc.finalLayout = isDepth
                                       ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                       : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // ready to sample in post-process
                attachmentDescriptions.push_back(desc);

                if (isDepth) {
                    depthRef.attachment = static_cast<uint32_t>(i);
                    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                    hasDepth = true;
                } else {
                    VkAttachmentReference ref{};
                    ref.attachment = static_cast<uint32_t>(i);
                    ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    colorRefs.push_back(ref);
                }
            }

            VkSubpassDescription subpass{};
            subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount = static_cast<uint32_t>(colorRefs.size());
            subpass.pColorAttachments = colorRefs.data();
            subpass.pDepthStencilAttachment = hasDepth ? &depthRef : nullptr;

            // Dependency ensures color attachment write finishes before post-process samples it
            std::array<VkSubpassDependency, 2> deps{};

            deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
            deps[0].dstSubpass = 0;
            deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

            deps[1].srcSubpass = 0;
            deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
            deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

            VkRenderPassCreateInfo renderPassInfo{};
            renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            renderPassInfo.attachmentCount = static_cast<uint32_t>(attachmentDescriptions.size());
            renderPassInfo.pAttachments = attachmentDescriptions.data();
            renderPassInfo.subpassCount = 1;
            renderPassInfo.pSubpasses = &subpass;
            renderPassInfo.dependencyCount = static_cast<uint32_t>(deps.size());
            renderPassInfo.pDependencies = deps.data();

            if (vkCreateRenderPass(device.device(), &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
                throw std::runtime_error("failed to create framebuffer render pass!");
            }
        }

        void Framebuffer::createFramebuffer() {
            std::vector<VkImageView> views;
            for (auto &attachment: attachments) {
                views.push_back(attachment.imageView);
            }

            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = renderPass;
            framebufferInfo.attachmentCount = static_cast<uint32_t>(views.size());
            framebufferInfo.pAttachments = views.data();
            framebufferInfo.width = extent.width;
            framebufferInfo.height = extent.height;
            framebufferInfo.layers = 1;

            if (vkCreateFramebuffer(device.device(), &framebufferInfo, nullptr, &framebuffer) != VK_SUCCESS) {
                throw std::runtime_error("failed to create framebuffer!");
            }
        }
    }
    */
