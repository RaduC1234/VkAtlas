#pragma once

#include "Device.hpp"
#include "ISwapChain.hpp"

namespace Atlas {
    class WindowSwapChain : public ISwapChain {
    public:
        WindowSwapChain(Device &device, VkExtent2D windowExtent);
        WindowSwapChain(Device &device, VkExtent2D windowExtent, std::shared_ptr<WindowSwapChain> oldSwapChain);
        ~WindowSwapChain() override;

        VkResult acquireNextImage(uint32_t *imageIndex) override;
        VkResult submitCommandBuffers(const VkCommandBuffer *buffers, uint32_t *imageIndex) override;

        VkFormat getImageFormat() const override { return swapChainImageFormat; }
        VkFormat getDepthFormat() const override { return swapChainDepthFormat; }
        VkExtent2D getExtent() const override { return swapChainExtent; }
        size_t imageCount() const override { return swapChainImages.size(); }
        VkRenderPass getRenderPass() const override { return renderPass; }
        VkFramebuffer getFrameBuffer(int32_t index) const override { return swapChainFramebuffers[index]; }

        VkFormat findDepthFormat();

    private:
        void init();
        void createSwapChain();
        void createImageViews();
        void createRenderPass();
        void createDepthResources();
        void createFramebuffers();
        void createSyncObjects();

        VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &availableFormats);
        VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR> &availablePresentModes);
        VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities);

        Device &device;
        VkExtent2D windowExtent;

        VkSwapchainKHR swapChain;
        std::shared_ptr<WindowSwapChain> oldSwapChain;

        VkFormat swapChainImageFormat;
        VkFormat swapChainDepthFormat;
        VkExtent2D swapChainExtent;
        VkRenderPass renderPass = VK_NULL_HANDLE;

        std::vector<VkImage> depthImages;
        std::vector<VmaAllocation> depthImageAllocations;
        std::vector<VkImageView> depthImageViews;

        std::vector<VkSemaphore> imageAvailableSemaphores;
        std::vector<VkSemaphore> renderFinishedSemaphores;
        std::vector<VkFence> inFlightFences;
        std::vector<VkFence> imagesInFlight;
        size_t currentFrame = 0;

        std::vector<VkFramebuffer> swapChainFramebuffers;
        std::vector<VkImage> swapChainImages;
        std::vector<VkImageView> swapChainImageViews;
    };
}
