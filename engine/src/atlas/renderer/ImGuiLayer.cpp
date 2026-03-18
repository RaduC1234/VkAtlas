#include "ImGuiLayer.hpp"

#include <stdexcept>

#ifndef __ANDROID__
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <imgui_impl_glfw.h>
#include <vulkan/vulkan.h>
#endif

namespace Atlas {
#ifdef __ANDROID__
    ImGuiLayer::ImGuiLayer(Device &device, Window &window, VkRenderPass renderPass, uint32_t imageCount) : device(VK_NULL_HANDLE) {}
    ImGuiLayer::~ImGuiLayer() {}
    void ImGuiLayer::beginFrame() {}
    void ImGuiLayer::endFrame(VkCommandBuffer commandBuffer) {}
#else
    ImGuiLayer::ImGuiLayer(Device &device, Window &window, VkRenderPass renderPass, uint32_t imageCount) : device(device) {
        if (device.getRenderMode() == RenderMode::XROnly) return;
        createDescriptorPool(device);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        ImGui::StyleColorsDark();

        ImGui_ImplGlfw_InitForVulkan(static_cast<GLFWwindow *>(window.getNativeHandle()), true);

        ImGui_ImplVulkan_InitInfo initInfo{};
        initInfo.Instance = device.getInstance();
        initInfo.PhysicalDevice = device.getPhysicalDevice();
        initInfo.Device = device.device();
        initInfo.QueueFamily = device.findPhysicalQueueFamilies().graphicsFamily.value();
        initInfo.Queue = device.graphicsQueue();
        initInfo.DescriptorPool = descriptorPool;
        initInfo.MinImageCount = 2;
        initInfo.ImageCount = imageCount;
        initInfo.UseDynamicRendering = false;
        initInfo.PipelineInfoMain.RenderPass = renderPass;
        initInfo.PipelineInfoMain.Subpass = 0;
        initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

        ImGui_ImplVulkan_Init(&initInfo);
    }

    ImGuiLayer::~ImGuiLayer() {
        if (device.getRenderMode() == RenderMode::XROnly) return;

        vkDeviceWaitIdle(device.device());

        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        if (descriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device.device(), descriptorPool, nullptr);
            descriptorPool = VK_NULL_HANDLE;
        }
    }

    void ImGuiLayer::beginFrame() {
        if (device.getRenderMode() == RenderMode::XROnly) return;

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    void ImGuiLayer::endFrame(VkCommandBuffer commandBuffer) {
        if (device.getRenderMode() == RenderMode::XROnly) return;

        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
    }

    void ImGuiLayer::createDescriptorPool(Device &device) {
        VkDescriptorPoolSize poolSizes[] = {
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
        };

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets = 1000;
        poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
        poolInfo.pPoolSizes = poolSizes;

        if (vkCreateDescriptorPool(device.device(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create ImGui descriptor pool");
        }
    }
#endif
}
