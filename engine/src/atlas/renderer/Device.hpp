#pragma once

#include <optional>
#include <vector>

#include "core/Window.hpp"

#include "vk_mem_alloc.h"
#include "utils/ExecutorService.hpp"

namespace Atlas {
#if defined(NDEBUG) || defined(__ANDROID__)
    constexpr bool enableValidationLayers = false;
#else
    constexpr bool enableValidationLayers = true;
#endif

    struct SwapChainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    struct QueueFamilyIndices {
        std::optional<uint32_t> graphicsFamily;
        std::optional<uint32_t> presentFamily;
        bool isComplete() const { return graphicsFamily.has_value() && presentFamily.has_value(); }
    };

    class Device {
    public:
        Device(Window &window);
        ~Device();

        Device(const Device &) = delete;
        Device &operator=(const Device &) = delete;
        Device(Device &&) = delete;
        Device &operator=(Device &&) = delete;

        VkDevice device() const { return device_; }
        VkSurfaceKHR surface() const { return surface_; }
        VkQueue graphicsQueue() const { return graphicsQueue_; }
        VkQueue presentQueue() const { return presentQueue_; }
        VkCommandPool getCommandPool() const { return commandPool; }
        const VkInstance &getInstance() const { return instance; }
        const VkPhysicalDevice &getPhysicalDevice() const { return physicalDevice; }
        const VmaAllocator &allocator() const { return this->allocator_; }
        ExecutorService &getExecutor() const { return *executor; }

        VkPhysicalDeviceProperties properties;

        SwapChainSupportDetails getSwapChainSupport() { return querySwapChainSupport(physicalDevice); }
        QueueFamilyIndices findPhysicalQueueFamilies() { return findQueueFamilies(physicalDevice); }

        uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
        VkFormat findSupportedFormat(const std::vector<VkFormat> &candidates, VkImageTiling tiling, VkFormatFeatureFlags features);

        VkCommandBuffer beginSingleTimeCommands();
        void endSingleTimeCommands(VkCommandBuffer commandBuffer) const;

    private:
        // Initialization
        void createInstance();
        void setupDebugMessenger();
        void createSurface();
        void pickPhysicalDevice();
        void createLogicalDevice();
        void createVmaAllocator();
        void createCommandPool();

        // Helper functions
        bool checkValidationLayerSupport();
        std::vector<const char *> getRequiredExtensions();
        VkPhysicalDevice findBestDevice(const std::vector<VkPhysicalDevice> &devices);
        void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT &DebugCreateInfo);
        QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
        bool checkDeviceExtensionSupport(VkPhysicalDevice device);
        void hasGflwRequiredInstanceExtensions();
        SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);

        // Core handles
        VkInstance instance;
        VkDebugUtilsMessengerEXT debugMessenger;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device_;
        VkSurfaceKHR surface_;
        VkQueue graphicsQueue_;
        VkQueue presentQueue_;
        VkCommandPool commandPool;
        Window &window;

        // Allocator & services
        VmaAllocator allocator_;
        std::unique_ptr<ExecutorService> executor;

        // Config
        const std::vector<const char *> validationLayers = {"VK_LAYER_KHRONOS_validation"};
        const std::vector<const char *> deviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME
        };
    };
}