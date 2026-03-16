#include "XrSwapChain.hpp"

#include "core/Log.hpp"

namespace Atlas {
    XrSwapChain::XrSwapChain(Device &device, XrSession session, const std::vector<XrViewConfigurationView> &viewType) : device(device) {
        createSwapChain(session, viewType);
        createImageViews();
        createDepthResources();
        createRenderPass();
        createFramebuffers();
    }

    XrSwapChain::~XrSwapChain() {
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

    void XrSwapChain::createSwapChain(XrSession session, const std::vector<XrViewConfigurationView>& viewConfigs) {
        this->extent = {viewConfigs[0].recommendedImageRectWidth, viewConfigs[0].recommendedImageRectHeight};
        this->imageFormat = VK_FORMAT_R8G8B8A8_SRGB;

        XrSwapchainCreateInfo createInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        createInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        createInfo.format = static_cast<int64_t>(imageFormat);
        createInfo.sampleCount = viewConfigs[0].recommendedSwapchainSampleCount;
        createInfo.width = extent.width;
        createInfo.height = extent.height;
        createInfo.faceCount = 1;
        createInfo.arraySize = static_cast<uint32_t>(viewConfigs.size());
        createInfo.mipCount = 1;

        if (xrCreateSwapchain(session, &createInfo, &xrSwapchain) != XR_SUCCESS) {
            throw std::runtime_error("Failed to create XR swapchain for eye " + std::to_string(eyeIndex));
        }

        uint32_t count = 0;
        xrEnumerateSwapchainImages(xrSwapchain, 0, &count, nullptr);

        std::vector xrImages(count, XrSwapchainImageVulkanKHR{XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
        xrEnumerateSwapchainImages(xrSwapchain, count, &count, reinterpret_cast<XrSwapchainImageBaseHeader *>(xrImages.data()));

        images.reserve(count);
        for (const auto &image: xrImages) {
            images.push_back(image.image);
        }
    }

    void XrSwapChain::createImageViews() {

    }

    void XrSwapChain::createDepthResources() {
    }

    void XrSwapChain::createRenderPass() {
    }

    void XrSwapChain::createFramebuffers() {
    }
}
