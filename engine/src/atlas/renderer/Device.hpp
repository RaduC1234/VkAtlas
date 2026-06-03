#pragma once

#include <functional>
#include <optional>
#include <vector>
#include <vk_mem_alloc.h>

#if defined(ATLAS_PROFILE_GPU)
#include <tracy/TracyVulkan.hpp>
#endif

#include "core/Window.hpp"
#include "utils/ExecutorService.hpp"

namespace Atlas {
#if defined(NDEBUG) || defined(__ANDROID__)
    constexpr bool enableValidationLayers = false;
#else
    constexpr bool enableValidationLayers = true;
#endif

#pragma region RayTracingFunctions
    struct RayTracingFunctions {
        PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR = nullptr;
        PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR = nullptr;
        PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR = nullptr;
        PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR = nullptr;
        PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR = nullptr;
        PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelinesKHR = nullptr;
        PFN_vkGetRayTracingShaderGroupHandlesKHR vkGetRayTracingShaderGroupHandlesKHR = nullptr;
        PFN_vkCmdTraceRaysKHR vkCmdTraceRaysKHR = nullptr;

        void load(VkDevice device) {
#define LOAD(name) \
name = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device, #name)); \
if (!name) throw std::runtime_error("Failed to load " #name);

            LOAD(vkCreateAccelerationStructureKHR)
            LOAD(vkDestroyAccelerationStructureKHR)
            LOAD(vkGetAccelerationStructureBuildSizesKHR)
            LOAD(vkCmdBuildAccelerationStructuresKHR)
            LOAD(vkGetAccelerationStructureDeviceAddressKHR)
            LOAD(vkCreateRayTracingPipelinesKHR)
            LOAD(vkGetRayTracingShaderGroupHandlesKHR)
            LOAD(vkCmdTraceRaysKHR)

#undef LOAD
        }

        static RayTracingFunctions &get();
    };
#pragma endregion

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
        struct CreateInfo {
            bool enableRayTracing = false;
        };

        Device(Window &window, CreateInfo createInfo);
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
        VkCommandPool getTransferCommandPool() const { return transferCommandPool_; }
        VkSemaphore transferTimelineSemaphore() const { return transferTimelineSemaphore_; }
        const VkInstance &getInstance() const { return vkInstance_; }
        const VkPhysicalDevice &getPhysicalDevice() const { return physicalDevice_; }
        const VmaAllocator &allocator() const { return allocator_; }
        ExecutorService &executor() const { return *executor_; }

        VkPhysicalDeviceProperties properties{};

        SwapChainSupportDetails getSwapChainSupport() { return querySwapChainSupport(physicalDevice_); }
        QueueFamilyIndices findPhysicalQueueFamilies() { return findQueueFamilies(physicalDevice_); }
        const QueueFamilyIndices &queueFamilyIndices() const { return queueFamilyIndices_; }
        const VkPhysicalDeviceRayTracingPipelinePropertiesKHR &rayTracingPipelineProperties() const { return rtPipelineProperties_; }
        const VkPhysicalDeviceAccelerationStructurePropertiesKHR &accelerationStructureProperties() const { return accelStructureProperties_; }
#if defined(ATLAS_PROFILE_GPU)
        TracyVkCtx gpuProfilerContext() const { return gpuProfilerContext_; }
#endif

        uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
        VkFormat findSupportedFormat(const std::vector<VkFormat> &candidates, VkImageTiling tiling, VkFormatFeatureFlags features);

        // Single time commands — synchronous, graphics queue
        VkCommandBuffer beginGraphicsCommands();
        void endGraphicsCommands(VkCommandBuffer commandBuffer) const;
        void submitGraphicsCommands(VkCommandBuffer commandBuffer, VkFence fence) const;
        void freeGraphicsCommandBuffer(VkCommandBuffer commandBuffer) const;

        // Transfer commands — async, transfer queue, one at a time
        struct TransferCmd {
            VkCommandBuffer buffer;
        };

        TransferCmd beginTransferCommands();
        uint64_t endTransferCommands(TransferCmd cmd,
                                     std::function<void(uint64_t)> onComplete = nullptr);

        bool isTransferComplete(uint64_t timelineValue) const;
        void pollTransferCallbacks();
        uint64_t currentTransferTimelineValue() const { return nextTransferTimelineValue_ - 1; }

        static const char *vkResultToString(VkResult result);

    private:
        static constexpr uint32_t APPLICATION_VERSION = VK_MAKE_VERSION(1, 0, 0);
        static constexpr const char *APPLICATION_NAME = "Atlas Engine";

        void createVkInstance();
        void setupDebugMessenger();
        void createSurface();
        void pickPhysicalDevice();
        void createLogicalDevice(CreateInfo createInfo);
        void createVmaAllocator();
        void createCommandPools();
        void createTransferCommandBuffer();
        void createTransferTimelineSemaphore();
#if defined(ATLAS_PROFILE_GPU)
        void createGpuProfilerContext();
#endif

        bool checkValidationLayerSupport();
        std::vector<const char *> getRequiredInstanceExtensions() const;
        std::vector<const char *> getRequiredDeviceExtensions() const;
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
        QueueFamilyIndices queueFamilyIndices_;
        VkCommandPool graphicsCommandPool_ = VK_NULL_HANDLE;
        VkCommandPool computeCommandPool_ = VK_NULL_HANDLE;
        VkCommandPool transferCommandPool_ = VK_NULL_HANDLE;

        // Single transfer command buffer — reset and reused each update()
        VkCommandBuffer transferCommandBuffer_ = VK_NULL_HANDLE;
        uint64_t lastTransferTimelineValue_ = 0;
        VkSemaphore transferTimelineSemaphore_ = VK_NULL_HANDLE;
        uint64_t nextTransferTimelineValue_ = 1;

        std::function<void(uint64_t)> pendingTransferCallback_;
        uint64_t pendingTransferSignalValue_ = 0;

        VmaAllocator allocator_ = VK_NULL_HANDLE;

        VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtPipelineProperties_{};
        VkPhysicalDeviceAccelerationStructurePropertiesKHR accelStructureProperties_{};
#if defined(ATLAS_PROFILE_GPU)
        TracyVkCtx gpuProfilerContext_ = nullptr;
#endif

        Window &window_;
        std::unique_ptr<ExecutorService> executor_;

        const std::vector<const char *> validationLayers_ = {
            "VK_LAYER_KHRONOS_validation"
        };
    };
} // namespace Atlas

#define VK_ERROR_TO_STRING(result) std::string(Device::vkResultToString((result)))

#define vkCreateAccelerationStructureKHR            Atlas::RayTracingFunctions::get().vkCreateAccelerationStructureKHR
#define vkDestroyAccelerationStructureKHR           Atlas::RayTracingFunctions::get().vkDestroyAccelerationStructureKHR
#define vkGetAccelerationStructureBuildSizesKHR     Atlas::RayTracingFunctions::get().vkGetAccelerationStructureBuildSizesKHR
#define vkCmdBuildAccelerationStructuresKHR         Atlas::RayTracingFunctions::get().vkCmdBuildAccelerationStructuresKHR
#define vkGetAccelerationStructureDeviceAddressKHR  Atlas::RayTracingFunctions::get().vkGetAccelerationStructureDeviceAddressKHR
#define vkCreateRayTracingPipelinesKHR              Atlas::RayTracingFunctions::get().vkCreateRayTracingPipelinesKHR
#define vkGetRayTracingShaderGroupHandlesKHR        Atlas::RayTracingFunctions::get().vkGetRayTracingShaderGroupHandlesKHR
#define vkCmdTraceRaysKHR                           Atlas::RayTracingFunctions::get().vkCmdTraceRaysKHR
