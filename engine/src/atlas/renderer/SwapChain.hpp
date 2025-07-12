#pragma once

#include "Device.hpp"

namespace Atlas {
    class SwapChain {
    public:
        static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

        SwapChain(Device &device, VkExtent2D windowExtent);
        SwapChain(Device &device, VkExtent2D windowExtent, std::shared_ptr<SwapChain> oldSwapChain);
        ~SwapChain();

        SwapChain(const SwapChain &) = delete;
        SwapChain &operator=(const SwapChain &) = delete;

        VkResult acquireNextImage(uint32_t *imageIndex) const;
        VkResult submitCommandBuffers(const VkCommandBuffer *buffers, uint32_t *imageIndex);

        VkFormat findDepthFormat();
        VkFormat getSwapChainImageFormat() const {return swapChainImageFormat; }
        VkExtent2D getSwapChainExtent() const { return swapChainExtent; }
        size_t imageCount() const { return swapChainImages.size(); }

        uint32_t width() const { return swapChainExtent.width; }
        uint32_t height() const { return swapChainExtent.height; }

        VkRenderPass getRenderPass() const { return renderPass; }
        VkFramebuffer getFrameBuffer(int32_t index) const { return swapChainFramebuffers[index]; }

        bool compareSwapFormats(const SwapChain& swapChain) const {
            return swapChain.swapChainDepthFormat == swapChainDepthFormat &&
                swapChain.swapChainImageFormat == swapChainImageFormat;
        }

    private:
        Device &device;
        VkExtent2D windowExtent;

        std::vector<VkFramebuffer> swapChainFramebuffers;
        VkRenderPass renderPass;

        std::vector<VkImage> depthImages;
        std::vector<VkDeviceMemory> depthImageMemorys;
        std::vector<VkImageView> depthImageViews;

        VkSwapchainKHR swapChain;
        std::shared_ptr<SwapChain> oldSwapChain;

        VkFormat swapChainImageFormat;
        VkFormat swapChainDepthFormat;
        VkExtent2D swapChainExtent;

        std::vector<VkImage> swapChainImages;
        std::vector<VkImageView> swapChainImageViews;

        std::vector<VkSemaphore> imageAvailableSemaphores;
        std::vector<VkSemaphore> renderFinishedSemaphores;
        std::vector<VkFence> inFlightFences;
        std::vector<VkFence> imagesInFlight;
        size_t currentFrame = 0;

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
    };
}
