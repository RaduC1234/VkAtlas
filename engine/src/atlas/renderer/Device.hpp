#pragma once

#include <optional>
#include <vector>

#include <vk_mem_alloc.h>
#include "core/Window.hpp"
#include "utils/ExecutorService.hpp"

namespace Atlas {
#if defined(NDEBUG) || defined(__ANDROID__)
    constexpr bool enableValidationLayers = false;
#else
    constexpr bool enableValidationLayers = true;
#endif

    struct SwapChainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities{};
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    struct QueueFamilyIndices {
        std::optional<uint32_t> graphicsFamily;
        std::optional<uint32_t> presentFamily;
        std::optional<uint32_t> computeFamily;
        std::optional<uint32_t> transferFamily;

        bool isComplete() const {
            return graphicsFamily.has_value() && presentFamily.has_value();
        }
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
        VkQueue computeQueue() const { return computeQueue_; }
        VkQueue transferQueue() const { return transferQueue_; }
        VkCommandPool getGraphicsCommandPool() const { return graphicsCommandPool_; }
        VkCommandPool getComputeCommandPool() const { return computeCommandPool_; }
        const VkInstance &getInstance() const { return vkInstance_; }
        const VkPhysicalDevice &getPhysicalDevice() const { return physicalDevice_; }
        const VmaAllocator &allocator() const { return allocator_; }
        ExecutorService &executor() const { return *executor_; }

        VkPhysicalDeviceProperties properties{};

        SwapChainSupportDetails getSwapChainSupport() { return querySwapChainSupport(physicalDevice_); }
        QueueFamilyIndices findPhysicalQueueFamilies() { return findQueueFamilies(physicalDevice_); }

        uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
        VkFormat findSupportedFormat(const std::vector<VkFormat> &candidates, VkImageTiling tiling, VkFormatFeatureFlags features);

        VkCommandBuffer beginSingleTimeCommands();
        void endSingleTimeCommands(VkCommandBuffer commandBuffer) const;

    private:
        static constexpr uint32_t APPLICATION_VERSION = VK_MAKE_VERSION(1, 0, 0);
        static constexpr const char *APPLICATION_NAME = "Atlas Engine";

        void createVkInstance();
        void setupDebugMessenger();
        void createSurface();
        void pickPhysicalDevice();
        void createLogicalDevice();
        void createVmaAllocator();
        void createCommandPools();

        bool checkValidationLayerSupport();
        std::vector<const char *> getRequiredExtensions() const;
        VkPhysicalDevice findBestDevice(const std::vector<VkPhysicalDevice> &devices);
        void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT &info);
        QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
        bool checkDeviceExtensionSupport(VkPhysicalDevice device);
        void outputRequiredInstanceExtensions(const std::vector<const char *> &required);
        SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);

        VkInstance vkInstance_ = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
        VkDevice device_ = VK_NULL_HANDLE;
        VkSurfaceKHR surface_ = VK_NULL_HANDLE;
        VkQueue graphicsQueue_ = VK_NULL_HANDLE;
        VkQueue presentQueue_ = VK_NULL_HANDLE;
        VkQueue computeQueue_ = VK_NULL_HANDLE;
        VkQueue transferQueue_ = VK_NULL_HANDLE;
        VkCommandPool graphicsCommandPool_ = VK_NULL_HANDLE;
        VkCommandPool computeCommandPool_ = VK_NULL_HANDLE;
        VmaAllocator allocator_ = VK_NULL_HANDLE;

        Window &window_;
        std::unique_ptr<ExecutorService> executor_;

        const std::vector<const char *> validationLayers_ = {
            "VK_LAYER_KHRONOS_validation"
        };
        const std::vector<const char *> deviceExtensions_ = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME
        };
    };
} // namespace Atlas
