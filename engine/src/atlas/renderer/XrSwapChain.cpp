#include "XrSwapChain.hpp"

#include "core/Log.hpp"

#include <stdexcept>
#include <array>

namespace Atlas {
    XrSwapChain::XrSwapChain(Device &device, XrSession session, const std::vector<XrViewConfigurationView> &viewConfigs)
        : device(device) {
        createSwapChain(session, viewConfigs);
        createImageViews();
        createDepthResources();
        createRenderPass();
        createFramebuffers();
    }

    XrSwapChain::~XrSwapChain() {
        for (auto view: imageViews) {
            vkDestroyImageView(device.device(), view, nullptr);
        }

        for (size_t i = 0; i < depthImages.size(); i++) {
            vkDestroyImageView(device.device(), depthImageViews[i], nullptr);
            vmaDestroyImage(device.allocator(), depthImages[i], depthImageAllocations[i]);
        }

        for (auto fb: framebuffers) {
            vkDestroyFramebuffer(device.device(), fb, nullptr);
        }

        if (renderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device.device(), renderPass, nullptr);
        }

        if (xrSwapchain != XR_NULL_HANDLE) {
            xrDestroySwapchain(xrSwapchain);
        }
    }

    VkResult XrSwapChain::acquireNextImage(uint32_t *imageIndex) {
        XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        if (xrAcquireSwapchainImage(xrSwapchain, &acquireInfo, imageIndex) != XR_SUCCESS) {
            return VK_ERROR_UNKNOWN;
        }

        XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        waitInfo.timeout = XR_INFINITE_DURATION;
        if (xrWaitSwapchainImage(xrSwapchain, &waitInfo) != XR_SUCCESS) {
            return VK_ERROR_UNKNOWN;
        }

        return VK_SUCCESS;
    }

    VkResult XrSwapChain::submitCommandBuffers(const VkCommandBuffer *buffers, uint32_t *imageIndex) {
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = buffers;

        if (vkQueueSubmit(device.graphicsQueue(), 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS) {
            return VK_ERROR_UNKNOWN;
        }

        vkQueueWaitIdle(device.graphicsQueue());

        XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        xrReleaseSwapchainImage(xrSwapchain, &releaseInfo);

        return VK_SUCCESS;
    }

    void XrSwapChain::createSwapChain(XrSession session, const std::vector<XrViewConfigurationView> &viewConfigs) {
        eyeCount = static_cast<uint32_t>(viewConfigs.size());
        extent = {viewConfigs[0].recommendedImageRectWidth, viewConfigs[0].recommendedImageRectHeight};
        imageFormat = VK_FORMAT_R8G8B8A8_SRGB;

        XrSwapchainCreateInfo createInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        createInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        createInfo.format = static_cast<int64_t>(imageFormat);
        createInfo.sampleCount = viewConfigs[0].recommendedSwapchainSampleCount;
        createInfo.width = extent.width;
        createInfo.height = extent.height;
        createInfo.faceCount = 1;
        createInfo.arraySize = eyeCount;
        createInfo.mipCount = 1;

        if (xrCreateSwapchain(session, &createInfo, &xrSwapchain) != XR_SUCCESS) {
            throw std::runtime_error("Failed to create XR swapchain");
        }

        uint32_t count = 0;
        xrEnumerateSwapchainImages(xrSwapchain, 0, &count, nullptr);

        std::vector<XrSwapchainImageVulkanKHR> xrImages(count, XrSwapchainImageVulkanKHR{XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
        xrEnumerateSwapchainImages(xrSwapchain, count, &count,
                                   reinterpret_cast<XrSwapchainImageBaseHeader *>(xrImages.data()));

        images.reserve(count);
        for (const auto &img: xrImages) {
            images.push_back(img.image);
        }
    }

    void XrSwapChain::createImageViews() {
        imageViews.resize(images.size());

        for (size_t i = 0; i < images.size(); i++) {
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = images[i];
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
            viewInfo.format = imageFormat;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = eyeCount;

            if (vkCreateImageView(device.device(), &viewInfo, nullptr, &imageViews[i]) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create XR swapchain image view");
            }
        }
    }

    void XrSwapChain::createDepthResources() {
        depthFormat = device.findSupportedFormat(
            {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
            VK_IMAGE_TILING_OPTIMAL,
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);

        depthImages.resize(images.size());
        depthImageAllocations.resize(images.size());
        depthImageViews.resize(images.size());

        for (size_t i = 0; i < images.size(); i++) {
            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.format = depthFormat;
            imageInfo.extent = {extent.width, extent.height, 1};
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = eyeCount;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

            if (vmaCreateImage(device.allocator(), &imageInfo, &allocInfo,
                               &depthImages[i], &depthImageAllocations[i], nullptr) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create XR depth image");
            }

            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = depthImages[i];
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
            viewInfo.format = depthFormat;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = eyeCount;

            if (vkCreateImageView(device.device(), &viewInfo, nullptr, &depthImageViews[i]) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create XR depth image view");
            }
        }
    }

    void XrSwapChain::createRenderPass() {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = imageFormat;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentDescription depthAttachment{};
        depthAttachment.format = depthFormat;
        depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorRef{};
        colorRef.attachment = 0;
        colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthRef{};
        depthRef.attachment = 1;
        depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
        subpass.pDepthStencilAttachment = &depthRef;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        // VK_KHR_multiview — render both eye layers in a single pass
        // viewMask bit N = render to layer N; correlationMask = GPU may tile them together
        uint32_t viewMask = (1u << eyeCount) - 1;
        uint32_t correlationMask = viewMask;

        VkRenderPassMultiviewCreateInfo multiviewInfo{};
        multiviewInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO;
        multiviewInfo.subpassCount = 1;
        multiviewInfo.pViewMasks = &viewMask;
        multiviewInfo.correlationMaskCount = 1;
        multiviewInfo.pCorrelationMasks = &correlationMask;

        std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.pNext = &multiviewInfo;
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        if (vkCreateRenderPass(device.device(), &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create XR render pass");
        }
    }

    void XrSwapChain::createFramebuffers() {
        framebuffers.resize(images.size());

        for (size_t i = 0; i < images.size(); i++) {
            std::array<VkImageView, 2> attachments = {imageViews[i], depthImageViews[i]};

            VkFramebufferCreateInfo fbInfo{};
            fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fbInfo.renderPass = renderPass;
            fbInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
            fbInfo.pAttachments = attachments.data();
            fbInfo.width = extent.width;
            fbInfo.height = extent.height;
            fbInfo.layers = 1; // multiview requires layers = 1; layer routing is done via viewMask

            if (vkCreateFramebuffer(device.device(), &fbInfo, nullptr, &framebuffers[i]) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create XR framebuffer");
            }
        }
    }
} // namespace Atlas
