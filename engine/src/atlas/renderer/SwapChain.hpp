#pragma once

#include "Device.hpp"

namespace Atlas {
    class SwapChain {
    public:
        SwapChain(Device &device, VkExtent2D windowExtent);

        ~SwapChain();

    private:
        Device &device;
        VkExtent2D windowExtent;

        VkSwapchainKHR swapChain;
        VkFormat swapChainImageFormat;
        VkExtent2D swapChainExtent;

        std::vector<VkImage> swapChainImages;
        std::vector<VkImageView> swapChainImageViews;


        void init();
        void createSwapChain();
        void createImageViews();

        VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &availableFormats);
        VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR> &availableFormats);
        VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities);
    };
}
