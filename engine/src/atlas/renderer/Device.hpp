#pragma once

#include <optional>
#include <vector>

#include "core/Window.hpp"

#include "vk_mem_alloc.h"
#include "utils/ExecutorService.hpp"

#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

namespace Atlas {
#if defined(NDEBUG) || defined(__ANDROID__)
    constexpr bool enableValidationLayers = false;
#else
    constexpr bool enableValidationLayers = true;
#endif

    enum RenderMode {
        WindowOnly,
        XROnly,
        Combined
    };

    struct SwapChainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    struct QueueFamilyIndices {
        std::optional<uint32_t> graphicsFamily;
        std::optional<uint32_t> presentFamily;
        std::optional<uint32_t> computeFamily;
        std::optional<uint32_t> transferFamily;
        bool isComplete() const { return graphicsFamily.has_value() && presentFamily.has_value(); }
    };

    class Device {
    public:
        Device(Window &window, RenderMode renderMode);
        ~Device();

        Device(const Device &) = delete;
        Device &operator=(const Device &) = delete;
        Device(Device &&) = delete;
        Device &operator=(Device &&) = delete;

        VkDevice device() const { return device_; }
        VkSurfaceKHR surface() const { return surface_; }
        VkQueue graphicsQueue() const { return graphicsQueue_; }
        VkQueue presentQueue() const { return presentQueue_; }
        VkQueue computeQueue() const { return computeQueue_; }
        VkQueue transferQueue() const { return transferQueue_; }
        VkCommandPool getGraphicsCommandPool() const { return graphicsCommandPool; }
        VkCommandPool getComputeCommandPool() const { return computeCommandPool; }
        const VkInstance &getInstance() const { return vkInstance; }
        const VkPhysicalDevice &getPhysicalDevice() const { return physicalDevice; }
        const VmaAllocator &allocator() const { return this->allocator_; }
        ExecutorService &getExecutor() const { return *executor; }

        VkPhysicalDeviceProperties properties;

        SwapChainSupportDetails getSwapChainSupport() { return querySwapChainSupport(physicalDevice); }
        QueueFamilyIndices findPhysicalQueueFamilies() { return findQueueFamilies(physicalDevice); }

        uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
        VkFormat findSupportedFormat(const std::vector<VkFormat> &candidates, VkImageTiling tiling, VkFormatFeatureFlags features);

        VkCommandBuffer beginTransferCommands();
        void endTransferCommands(VkCommandBuffer commandBuffer) const;

        VkCommandBuffer beginGraphicsCommands();
        void endGraphicsCommands(VkCommandBuffer commandBuffer) const;

    private:
        static constexpr uint32_t APPLICATION_VERSION = VK_MAKE_VERSION(1, 0, 0);
        static constexpr const char* APPLICATION_NAME = "Atlas Engine";

        // Initialization
        void createVkInstance();
        void createXrInstance();
        void setupDebugMessenger();
        void createSurface();
        void pickPhysicalDevice();
        void createLogicalDevice();
        void createVmaAllocator();
        void createCommandPools();

        // Helper functions
        bool checkValidationLayerSupport();
        std::vector<const char *> getRequiredExtensions() const;
        VkPhysicalDevice findBestDevice(const std::vector<VkPhysicalDevice> &devices);
        void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT &DebugCreateInfo);
        QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
        bool checkDeviceExtensionSupport(VkPhysicalDevice device);
        void outputRequiredInstanceExtensions(const std::vector<const char *> &requiredExtensions);
        SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);

        // Core handles
        VkInstance vkInstance;
        VkDebugUtilsMessengerEXT debugMessenger;
        VkPhysicalDevice physicalDevice;
        VkDevice device_;
        VkSurfaceKHR surface_;
        VkQueue graphicsQueue_;
        VkQueue presentQueue_;
        VkQueue computeQueue_;
        VkQueue transferQueue_;
        VkCommandPool graphicsCommandPool;
        VkCommandPool computeCommandPool;
        Window &window;
        RenderMode renderMode;

        XrInstance xrInstance = XR_NULL_HANDLE;
        XrSystemId xrSystemId= XR_NULL_SYSTEM_ID;

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
