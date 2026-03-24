#include "Device.hpp"

#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

#include "core/Log.hpp"

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

namespace Atlas {
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData, void *pUserData) {
        const char *message = pCallbackData && pCallbackData->pMessage ? pCallbackData->pMessage : "(no message)";

        if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) {
            AT_TRACE(message);
        } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
            AT_INFO(message);
        } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
            AT_WARN(message);
        } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
            AT_ERROR(message);
        } else {
            AT_INFO(message);
        }

        return VK_FALSE;
    }

    VkResult createDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDebugUtilsMessengerEXT *pDebugMessenger) {
        auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
        if (func != nullptr) {
            return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
        } else {
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }
    }

    void destroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks *pAllocator) {
        auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (func != nullptr) {
            func(instance, debugMessenger, pAllocator);
        }
    }

    template<typename PFN>
    static void loadXrProc(const XrInstance instance, const char *name, PFN &outFn) {
        if (xrGetInstanceProcAddr(instance, name, reinterpret_cast<PFN_xrVoidFunction *>(&outFn)) != XR_SUCCESS || outFn == nullptr) {
            throw std::runtime_error(std::string("Failed to load OpenXR function: ") + name);
        }
    }

    Device::Device(Window &window, RenderMode renderMode) : window_{window}, renderMode_{renderMode} {
        printRenderMode();

        if (renderMode == RenderMode::XROnly || renderMode == RenderMode::Combined) {
            createXrInstance();
            loadXrVulkanExtensions();
        }

        createVkInstance();
        setupDebugMessenger();

        if (renderMode == RenderMode::WindowOnly || renderMode == RenderMode::Combined) {
            createSurface();
        }

        pickPhysicalDevice();
        createLogicalDevice();
        createVmaAllocator();
        createCommandPools();

        if (renderMode == RenderMode::XROnly || renderMode == RenderMode::Combined) {
            createXrSession();
        }

        this->executor_ = std::make_unique<ExecutorService>();
    }

    Device::~Device() {
        if (xrSession_ != XR_NULL_HANDLE) {
            xrDestroySession(xrSession_);
        }
        if (xrInstance_ != XR_NULL_HANDLE) {
            xrDestroyInstance(xrInstance_);
        }

        vkDestroyCommandPool(device_, graphicsCommandPool_, nullptr);
        vkDestroyCommandPool(device_, computeCommandPool_, nullptr);
        vmaDestroyAllocator(allocator_);
        vkDestroyDevice(device_, nullptr);

        if constexpr (enableValidationLayers) {
            destroyDebugUtilsMessengerEXT(vkInstance_, debugMessenger_, nullptr);
        }

        vkDestroySurfaceKHR(vkInstance_, surface_, nullptr);
        vkDestroyInstance(vkInstance_, nullptr);
    }

    void Device::printRenderMode() {
        switch (renderMode_) {
            case RenderMode::WindowOnly: AT_INFO("Render mode: Window only");
                break;
            case RenderMode::XROnly: AT_INFO("Render mode: XR only");
                break;
            case RenderMode::Combined: AT_INFO("Render mode: Combined (XR + Window)");
                break;
        }
    }

    void Device::createXrInstance() {
        const std::vector<const char *> extensions = {
            XR_KHR_VULKAN_ENABLE_EXTENSION_NAME,
            XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME
        };

        XrInstanceCreateInfo createInfo{XR_TYPE_INSTANCE_CREATE_INFO};
        createInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
        createInfo.applicationInfo.applicationVersion = APPLICATION_VERSION;
        createInfo.applicationInfo.engineVersion = APPLICATION_VERSION;
        std::strncpy(createInfo.applicationInfo.applicationName, APPLICATION_NAME, XR_MAX_APPLICATION_NAME_SIZE - 1);
        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInfo.enabledExtensionNames = extensions.data();

        if (xrCreateInstance(&createInfo, &xrInstance_) != XR_SUCCESS) {
            throw std::runtime_error("Failed to create OpenXR instance");
        }

        XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
        systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

        const XrResult result = xrGetSystem(xrInstance_, &systemInfo, &xrSystemId_);
        if (result == XR_ERROR_FORM_FACTOR_UNAVAILABLE) {
            AT_WARN("No HMD detected — connect a headset or enable a simulator runtime.");
            return;
        }
        if (result != XR_SUCCESS) {
            throw std::runtime_error("xrGetSystem failed");
        }

        AT_TRACE("OpenXR instance and system created.");
    }

    void Device::loadXrVulkanExtensions() {
        auto parseSpaceSeparated = [](const std::string &list, std::vector<std::string> &out) {
            out.clear();
            std::istringstream ss(list);
            for (std::string tok; ss >> tok;) out.push_back(std::move(tok));
        };

        PFN_xrGetVulkanInstanceExtensionsKHR pfnInstanceExts = nullptr;
        loadXrProc(xrInstance_, "xrGetVulkanInstanceExtensionsKHR", pfnInstanceExts);

        uint32_t instanceSize = 0;
        pfnInstanceExts(xrInstance_, xrSystemId_, 0, &instanceSize, nullptr);
        std::string instanceList(instanceSize, '\0');
        pfnInstanceExts(xrInstance_, xrSystemId_, instanceSize, &instanceSize, instanceList.data());
        parseSpaceSeparated(instanceList, xrInstanceExtensionBuffer_);

        PFN_xrGetVulkanDeviceExtensionsKHR pfnDeviceExts = nullptr;
        loadXrProc(xrInstance_, "xrGetVulkanDeviceExtensionsKHR", pfnDeviceExts);

        uint32_t deviceSize = 0;
        pfnDeviceExts(xrInstance_, xrSystemId_, 0, &deviceSize, nullptr);
        std::string deviceList(deviceSize, '\0');
        pfnDeviceExts(xrInstance_, xrSystemId_, deviceSize, &deviceSize, deviceList.data());
        parseSpaceSeparated(deviceList, xrDeviceExtensionBuffer_);

        std::stringstream log;
        log << "XR instance extensions (" << xrInstanceExtensionBuffer_.size() << "):\n";
        for (const auto &e: xrInstanceExtensionBuffer_) log << "  " << e << "\n";
        log << "XR device extensions (" << xrDeviceExtensionBuffer_.size() << "):\n";
        for (const auto &e: xrDeviceExtensionBuffer_) log << "  " << e << "\n";
        AT_INFO(log.str());
    }

    void Device::createXrSession() {
        if (xrSystemId_ == XR_NULL_SYSTEM_ID) {
            AT_WARN("Skipping XR session — no system available.");
            return;
        }

        uint32_t viewCount = 0;
        xrEnumerateViewConfigurationViews(xrInstance_, xrSystemId_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, nullptr);

        xrViewConfigViews_.assign(viewCount, XrViewConfigurationView{XR_TYPE_VIEW_CONFIGURATION_VIEW});
        xrEnumerateViewConfigurationViews(xrInstance_, xrSystemId_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, viewCount, &viewCount, xrViewConfigViews_.data());

        AT_INFO("XR stereo view count: {}", viewCount);

        XrGraphicsBindingVulkanKHR binding{XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR};
        binding.instance = vkInstance_;
        binding.physicalDevice = physicalDevice_;
        binding.device = device_;
        binding.queueFamilyIndex = findQueueFamilies(physicalDevice_).graphicsFamily.value();
        binding.queueIndex = 0;

        PFN_xrGetVulkanGraphicsRequirementsKHR pfnGetRequirements = nullptr;
        loadXrProc(xrInstance_, "xrGetVulkanGraphicsRequirementsKHR", pfnGetRequirements);

        XrGraphicsRequirementsVulkanKHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR};
        if (pfnGetRequirements(xrInstance_, xrSystemId_, &requirements) != XR_SUCCESS) {
            throw std::runtime_error("xrGetVulkanGraphicsRequirementsKHR failed");
        }

        XrSessionCreateInfo sci{XR_TYPE_SESSION_CREATE_INFO};
        sci.next = &binding;
        sci.systemId = xrSystemId_;

        if (XrResult result = xrCreateSession(xrInstance_, &sci, &xrSession_); result != XR_SUCCESS) {
            throw std::runtime_error(std::string("Failed to create XR session, result: ") + std::to_string(result));
        }

        AT_TRACE("XR session created.");
    }

    void Device::populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT &createInfo) {
        createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        createInfo.pfnUserCallback = debugCallback;
        createInfo.pUserData = nullptr; // Optional
    }

    void Device::createVkInstance() {
        if (enableValidationLayers && !checkValidationLayerSupport()) {
            throw std::runtime_error("Validation layers requested but not available");
        }

        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = APPLICATION_NAME;
        appInfo.applicationVersion = APPLICATION_VERSION;
        appInfo.pEngineName = APPLICATION_NAME;
        appInfo.engineVersion = APPLICATION_VERSION;
        appInfo.apiVersion = VK_API_VERSION_1_3;

        VkInstanceCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;

        auto extensions = getRequiredExtensions();
        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();

        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo;
        if constexpr (enableValidationLayers) {
            createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers_.size());
            createInfo.ppEnabledLayerNames = validationLayers_.data();

            populateDebugMessengerCreateInfo(debugCreateInfo);
            createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT *) &debugCreateInfo;
        } else {
            createInfo.enabledLayerCount = 0;
            createInfo.pNext = nullptr;
        }

        if (vkCreateInstance(&createInfo, nullptr, &vkInstance_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create instance!");
        }

        outputRequiredInstanceExtensions(extensions);
    }

    void Device::setupDebugMessenger() {
        if constexpr (!enableValidationLayers) {
            return;
        }

        VkDebugUtilsMessengerCreateInfoEXT createInfo;
        populateDebugMessengerCreateInfo(createInfo);

        if (createDebugUtilsMessengerEXT(vkInstance_, &createInfo, nullptr, &debugMessenger_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to set up debug messenger!");
        }
    }

    void Device::createSurface() {
        window_.createWindowSurface(vkInstance_, &surface_);
    }

    void Device::pickPhysicalDevice() {
        if (renderMode_ == RenderMode::XROnly || renderMode_ == RenderMode::Combined) {
            // The XR runtime nominates the physical device — using any other will fail at session creation.
            PFN_xrGetVulkanGraphicsDeviceKHR pfnGetDevice = nullptr;
            loadXrProc(xrInstance_, "xrGetVulkanGraphicsDeviceKHR", pfnGetDevice);

            VkPhysicalDevice xrDevice = VK_NULL_HANDLE;
            pfnGetDevice(xrInstance_, xrSystemId_, vkInstance_, &xrDevice);

            if (xrDevice == VK_NULL_HANDLE) {
                throw std::runtime_error("OpenXR returned a null physical device");
            }
            this->physicalDevice_ = xrDevice;
        } else {
            uint32_t deviceCount = 0;
            vkEnumeratePhysicalDevices(vkInstance_, &deviceCount, nullptr);

            std::vector<VkPhysicalDevice> devices(deviceCount);
            vkEnumeratePhysicalDevices(vkInstance_, &deviceCount, devices.data());

            this->physicalDevice_ = findBestDevice(devices);
            if (physicalDevice_ == VK_NULL_HANDLE) {
                throw std::runtime_error("No suitable GPU found");
            }
        }

        vkGetPhysicalDeviceProperties(physicalDevice_, &properties);
        AT_INFO("Physical device: {}", properties.deviceName);
    }

    VkPhysicalDevice Device::findBestDevice(const std::vector<VkPhysicalDevice> &devices) {
        VkPhysicalDevice best = VK_NULL_HANDLE;
        int bestScore = 0;

        for (const auto &dev: devices) {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(dev, &props);

            VkPhysicalDeviceFeatures feats;
            vkGetPhysicalDeviceFeatures(dev, &feats);

            const QueueFamilyIndices idx = findQueueFamilies(dev);
            if (!idx.isComplete() || !checkDeviceExtensionSupport(dev) || !feats.samplerAnisotropy)
                continue;

            const SwapChainSupportDetails sc = querySwapChainSupport(dev);
            if (sc.formats.empty() || sc.presentModes.empty()) continue;

            int score = 0;
            if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) score += 1000;
            else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score += 500;
            score += static_cast<int>(props.limits.maxImageDimension2D);

            if (score > bestScore) {
                bestScore = score;
                best = dev;
            }
        }
        return best;
    }

    void Device::createLogicalDevice() {
        const QueueFamilyIndices indices = findQueueFamilies(physicalDevice_);

        const std::set<uint32_t> uniqueFamilies = {
            indices.graphicsFamily.value(),
            indices.presentFamily.value(),
            indices.computeFamily.value(),
            indices.transferFamily.value()
        };

        constexpr float queuePriority = 1.0f;
        std::vector<VkDeviceQueueCreateInfo> queueCIs;
        queueCIs.reserve(uniqueFamilies.size());
        for (uint32_t family: uniqueFamilies) {
            VkDeviceQueueCreateInfo queueCreateInfo{};
            queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo.queueFamilyIndex = family;
            queueCreateInfo.queueCount = 1;
            queueCreateInfo.pQueuePriorities = &queuePriority;
            queueCIs.push_back(queueCreateInfo);
        }

        VkPhysicalDeviceVulkan12Features vk12{};
        vk12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        vk12.timelineSemaphore = VK_TRUE;
        vk12.runtimeDescriptorArray = VK_TRUE;
        vk12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
        vk12.descriptorBindingPartiallyBound = VK_TRUE;
        vk12.descriptorBindingVariableDescriptorCount = VK_TRUE;
        vk12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;

        VkPhysicalDeviceVulkan11Features vk11{};
        vk11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
        vk11.pNext = &vk12;
        vk11.shaderDrawParameters = VK_TRUE;
        vk11.multiview = VK_TRUE;

        VkPhysicalDeviceFeatures2 feats2{};
        feats2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        feats2.pNext = &vk11;
        vkGetPhysicalDeviceFeatures2(physicalDevice_, &feats2);

        if (!vk12.runtimeDescriptorArray || !vk12.shaderSampledImageArrayNonUniformIndexing) {
            throw std::runtime_error("GPU lacks descriptor indexing features required for bindless textures");
        }

        VkPhysicalDeviceFeatures coreFeatures{};
        coreFeatures.samplerAnisotropy = VK_TRUE;
        coreFeatures.multiDrawIndirect = VK_TRUE;

        std::vector<const char *> allExts = deviceExtensions_;
        for (const auto &xrExt: xrDeviceExtensionBuffer_) {
            const bool exists = std::ranges::any_of(allExts, [&](const char *e) { return strcmp(e, xrExt.c_str()) == 0; });
            if (!exists) {
                allExts.push_back(xrExt.c_str());
            }
        }

        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.pNext = &vk11;
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCIs.size());
        createInfo.pQueueCreateInfos = queueCIs.data();
        createInfo.pEnabledFeatures = &coreFeatures;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(allExts.size());
        createInfo.ppEnabledExtensionNames = allExts.data();

        if constexpr (enableValidationLayers) {
            createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers_.size());
            createInfo.ppEnabledLayerNames = validationLayers_.data();
        }

        if (vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create logical device");
        }

        vkGetDeviceQueue(device_, indices.graphicsFamily.value(), 0, &graphicsQueue_);
        vkGetDeviceQueue(device_, indices.presentFamily.value(), 0, &presentQueue_);
        vkGetDeviceQueue(device_, indices.computeFamily.value(), 0, &computeQueue_);
        vkGetDeviceQueue(device_, indices.transferFamily.value(), 0, &transferQueue_);

        AT_INFO("Logical device created. Max push constant size: {} bytes", properties.limits.maxPushConstantsSize);
    }

    void Device::createVmaAllocator() {
        VmaAllocatorCreateInfo allocatorInfo{};
        allocatorInfo.physicalDevice = this->physicalDevice_;
        allocatorInfo.device = this->device_;
        allocatorInfo.instance = this->vkInstance_;

        if (vmaCreateAllocator(&allocatorInfo, &allocator_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create VMA allocator");
        }
    }

    void Device::createCommandPools() {
        QueueFamilyIndices queueFamilyIndices = findPhysicalQueueFamilies();
        VkCommandPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;

        poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        if (vkCreateCommandPool(device_, &poolInfo, nullptr, &graphicsCommandPool_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create graphics command pool!");
        }

        poolInfo.queueFamilyIndex = queueFamilyIndices.computeFamily.value();
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        if (vkCreateCommandPool(device_, &poolInfo, nullptr, &computeCommandPool_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create compute command pool!");
        }
    }

    bool Device::checkValidationLayerSupport() {
        uint32_t layerCount;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

        for (const char *layerName: validationLayers_) {
            bool layerFound = false;

            for (const auto &layerProperties: availableLayers) {
                if (strcmp(layerName, layerProperties.layerName) == 0) {
                    layerFound = true;
                    break;
                }
            }

            if (!layerFound) {
                return false;
            }
        }

        return true;
    }

    std::vector<const char *> Device::getRequiredExtensions() const {
        auto extensions = (renderMode_ == RenderMode::WindowOnly || renderMode_ == RenderMode::Combined)
                              ? window_.getRequiredExtensions()
                              : std::vector<const char *>{};

        if constexpr (enableValidationLayers) {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        for (const auto &e: xrInstanceExtensionBuffer_) {
            const bool exists = std::ranges::any_of(extensions, [&](const char *ex) { return strcmp(ex, e.c_str()) == 0; });
            if (!exists) extensions.push_back(e.c_str());
        }

        return extensions;
    }

    void Device::outputRequiredInstanceExtensions(const std::vector<const char *> &required) {
        uint32_t count = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
        std::vector<VkExtensionProperties> available(count);
        vkEnumerateInstanceExtensionProperties(nullptr, &count, available.data());

        std::unordered_set<std::string> availableSet;
        availableSet.reserve(count);
        for (const auto &e: available) availableSet.insert(e.extensionName);

        std::stringstream ss;
        ss << "Required Vulkan instance extensions:\n";
        for (const char *r: required) {
            const bool present = availableSet.contains(r);
            ss << "  [" << (present ? "OK" : "MISSING") << "] " << r << "\n";
            if (!present) throw std::runtime_error(std::string("Missing required extension: ") + r);
        }
        AT_INFO(ss.str());
    }

    QueueFamilyIndices Device::findQueueFamilies(VkPhysicalDevice device) {
        QueueFamilyIndices indices;

        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

        for (uint32_t i = 0; i < queueFamilies.size(); i++) {
            const auto &family = queueFamilies[i];

            if (family.queueCount == 0) continue;

            if (family.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                indices.graphicsFamily = i;
            }

            if (surface_ != VK_NULL_HANDLE) {
                VkBool32 presentSupport = false;
                vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_, &presentSupport);
                if (presentSupport) {
                    indices.presentFamily = i;
                }
            } else {
                indices.presentFamily = indices.graphicsFamily;
            }

            if ((family.queueFlags & VK_QUEUE_COMPUTE_BIT) && !(family.queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                indices.computeFamily = i;
            }

            if ((family.queueFlags & VK_QUEUE_TRANSFER_BIT) && !(family.queueFlags & VK_QUEUE_GRAPHICS_BIT) && !(family.queueFlags & VK_QUEUE_COMPUTE_BIT)) {
                indices.transferFamily = i;
            }
        }

        if (!indices.computeFamily.has_value()) {
            indices.computeFamily = indices.graphicsFamily;
        }

        if (!indices.transferFamily.has_value()) {
            indices.transferFamily = indices.graphicsFamily;
        }

        return indices;
    }

    bool Device::checkDeviceExtensionSupport(VkPhysicalDevice device) {
        uint32_t extensionCount;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(
            device,
            nullptr,
            &extensionCount,
            availableExtensions.data());

        std::set<std::string> requiredExtensions(deviceExtensions_.begin(), deviceExtensions_.end());

        for (const auto &extension: availableExtensions) {
            requiredExtensions.erase(extension.extensionName);
        }

        return requiredExtensions.empty();
    }

    SwapChainSupportDetails Device::querySwapChainSupport(VkPhysicalDevice device) {
        SwapChainSupportDetails details;

        if (surface_ == VK_NULL_HANDLE) {
            return details;
        }

        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface_, &details.capabilities);

        uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &formatCount, nullptr);
        if (formatCount > 0) {
            details.formats.resize(formatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &formatCount, details.formats.data());
        }

        uint32_t modeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &modeCount, nullptr);
        if (modeCount > 0) {
            details.presentModes.resize(modeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &modeCount, details.presentModes.data());
        }

        return details;
    }

    uint32_t Device::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags requiredFlags) {
        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProps);

        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
            if ((typeFilter & (1u << i)) &&
                (memProps.memoryTypes[i].propertyFlags & requiredFlags) == requiredFlags)
                return i;
        }
        throw std::runtime_error("Failed to find suitable memory type");
    }

    VkFormat Device::findSupportedFormat(const std::vector<VkFormat> &candidates, VkImageTiling tiling, VkFormatFeatureFlags features) {
        for (const VkFormat format: candidates) {
            VkFormatProperties props;
            vkGetPhysicalDeviceFormatProperties(physicalDevice_, format, &props);

            if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
                return format;
            } else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
                return format;
            }
        }
        throw std::runtime_error("failed to find supported format!");
    }

    VkCommandBuffer Device::beginSingleTimeCommands() {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = graphicsCommandPool_; // use graphics for now until asset streaming is implemented
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers(device_, &allocInfo, &commandBuffer);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(commandBuffer, &beginInfo);
        return commandBuffer;
    }

    void Device::endSingleTimeCommands(VkCommandBuffer commandBuffer) const {
        vkEndCommandBuffer(commandBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        vkQueueSubmit(graphicsQueue_, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphicsQueue_);

        vkFreeCommandBuffers(device_, graphicsCommandPool_, 1, &commandBuffer);
    }
} // namespace Atlas
