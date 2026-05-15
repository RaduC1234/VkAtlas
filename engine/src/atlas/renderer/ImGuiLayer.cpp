#include "ImGuiLayer.hpp"

#include <array>
#include <stdexcept>

#if defined(ATLAS_PLATFORM_DESKTOP)
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#endif

namespace Atlas {
#if defined(ATLAS_PLATFORM_DESKTOP)
    ImGuiLayer::ImGuiLayer(Device &device, Window &window, VkRenderPass renderPass, uint32_t imageCount) : device(device.device()) {
        createDescriptorPool(device);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.Fonts->AddFontFromFileTTF("assets/engine/Roboto-Medium.ttf", 15.0f);

        setStyle();

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
        if (device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device);
            ImGui_ImplVulkan_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();

            if (descriptorPool != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(device, descriptorPool, nullptr);
                descriptorPool = VK_NULL_HANDLE;
            }
        }
    }

    void ImGuiLayer::beginFrame(bool createDockSpace) {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (!createDockSpace) {
            return;
        }

        ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

        constexpr ImGuiWindowFlags hostFlags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoNavFocus;

        ImGui::Begin("DockspaceHost", nullptr, hostFlags);
        ImGui::PopStyleVar(3);

        const ImGuiID dockspaceId = ImGui::GetID("MainDockspaceRoot");
        ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f));
        ImGui::End();
    }

    void ImGuiLayer::endFrame() {
        ImGui::Render();
    }

    void ImGuiLayer::renderDrawData(VkCommandBuffer commandBuffer) {
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
    }

    VkDescriptorSet ImGuiLayer::addTexture(VkImageView imageView, VkImageLayout imageLayout) {
        return addTexture(VK_NULL_HANDLE, imageView, imageLayout);
    }

    VkDescriptorSet ImGuiLayer::addTexture(VkSampler sampler, VkImageView imageView, VkImageLayout imageLayout) {
        return ImGui_ImplVulkan_AddTexture(sampler, imageView, imageLayout);
    }

    void ImGuiLayer::removeTexture(VkDescriptorSet texture) {
        ImGui_ImplVulkan_RemoveTexture(texture);
    }

    void ImGuiLayer::createDescriptorPool(Device &device) {
        const std::array<VkDescriptorPoolSize, 3> poolSizes{{
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
        }};

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets = 1000;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();

        if (vkCreateDescriptorPool(device.device(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create ImGui descriptor pool");
        }
    }

    void ImGuiLayer::setStyle() {
        ImGuiStyle &style = ImGui::GetStyle();
        style.WindowRounding = 0.0f;
        style.FrameRounding = 4.0f;
        style.GrabRounding = 4.0f;
        style.TabRounding = 4.0f;

        ImVec4 *colors = style.Colors;
        colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.09f, 0.10f, 1.00f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.09f, 0.10f, 0.12f, 1.00f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.13f, 0.15f, 1.00f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.17f, 0.18f, 0.20f, 1.00f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.21f, 0.23f, 1.00f);
        colors[ImGuiCol_TitleBg] = ImVec4(0.05f, 0.06f, 0.07f, 1.00f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.08f, 0.09f, 0.10f, 1.00f);
        colors[ImGuiCol_Button] = ImVec4(0.16f, 0.17f, 0.19f, 1.00f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.22f, 0.24f, 0.27f, 1.00f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.26f, 0.28f, 0.31f, 1.00f);
        colors[ImGuiCol_Header] = ImVec4(0.16f, 0.18f, 0.22f, 1.00f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.22f, 0.25f, 0.30f, 1.00f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.29f, 0.34f, 1.00f);
    }
#else
    ImGuiLayer::ImGuiLayer(Device &, Window &, VkRenderPass, uint32_t) {}
    ImGuiLayer::~ImGuiLayer() = default;
    void ImGuiLayer::beginFrame(bool) {}
    void ImGuiLayer::endFrame() {}
    void ImGuiLayer::renderDrawData(VkCommandBuffer) {}
    VkDescriptorSet ImGuiLayer::addTexture(VkImageView, VkImageLayout) { return VK_NULL_HANDLE; }
    VkDescriptorSet ImGuiLayer::addTexture(VkSampler, VkImageView, VkImageLayout) { return VK_NULL_HANDLE; }
    void ImGuiLayer::removeTexture(VkDescriptorSet) {}
#endif
}
