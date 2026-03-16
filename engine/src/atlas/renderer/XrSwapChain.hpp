#pragma once

#include "Device.hpp"
#include "ISwapChain.hpp"

namespace Atlas {
    class XrSwapChain : public ISwapChain {
    public:
        XrSwapChain(Device &device, XrSession session, const std::vector<XrViewConfigurationView>& viewType);
        ~XrSwapChain() override;

        VkResult acquireNextImage(uint32_t *imageIndex) override;
        VkResult submitCommandBuffers(const VkCommandBuffer *buffers, uint32_t *imageIndex) override;

        VkFormat getImageFormat() const override { return imageFormat; }
        VkFormat getDepthFormat() const override { return depthFormat; }
        VkExtent2D getExtent() const override { return extent; }
        size_t imageCount() const override { return images.size(); }
        VkRenderPass getRenderPass() const override { return renderPass; }
        VkFramebuffer getFrameBuffer(int32_t index) const override { return framebuffers[index]; }

    private:
        void createSwapChain(XrSession session, const std::vector<XrViewConfigurationView> &viewConfigs);
        void createImageViews();
        void createDepthResources();
        void createRenderPass();
        void createFramebuffers();

        Device &device;
        uint32_t eyeIndex;

        XrSwapchain xrSwapchain = XR_NULL_HANDLE;

        VkFormat imageFormat;
        VkFormat depthFormat;
        VkExtent2D extent;

        std::vector<VkImage> images;
        std::vector<VkImageView> imageViews;
        std::vector<VkFramebuffer> framebuffers;
        VkRenderPass renderPass = VK_NULL_HANDLE;

        std::vector<VkImage> depthImages;
        std::vector<VmaAllocation> depthImageAllocations;
        std::vector<VkImageView> depthImageViews;
    };
}
