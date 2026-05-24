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
    ImGuiLayer::ImGuiLayer(Device &device, Window &window, VkRenderPass renderPass, uint32_t imageCount) : device(device.device()), nativeWindow(window.getNativeHandle()) {
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

        if (!createDockSpace)
            return;

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
        constexpr std::array<VkDescriptorPoolSize, 3> poolSizes{
            {
                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
                {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
            }
        };

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets = 1000;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();

        if (vkCreateDescriptorPool(device.device(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS)
            throw std::runtime_error("Failed to create ImGui descriptor pool");
    }

    void ImGuiLayer::setStyle() {
        ImGuiStyle &style = ImGui::GetStyle();
        style.WindowRounding = 0.0f;
        style.ChildRounding = 0.0f;
        style.PopupRounding = 2.0f;
        style.FrameRounding = 1.0f;
        style.GrabRounding = 2.0f;
        style.TabRounding = 1.0f;
        style.ScrollbarRounding = 2.0f;
        style.WindowBorderSize = 1.0f;
        style.FrameBorderSize = 0.0f;
        style.TabBorderSize = 0.0f;
        style.WindowPadding = ImVec2(7.0f, 5.0f);
        style.FramePadding = ImVec2(6.0f, 3.0f);
        style.ItemSpacing = ImVec2(6.0f, 3.0f);
        style.ScrollbarSize = 10.0f;

        ImVec4 *c = style.Colors;
        c[ImGuiCol_Text] = srgb(ImVec4(0.860f, 0.860f, 0.840f, 1.00f));
        c[ImGuiCol_TextDisabled] = srgb(ImVec4(0.460f, 0.460f, 0.440f, 1.00f));
        c[ImGuiCol_WindowBg] = srgb(ImVec4(0.190f, 0.190f, 0.190f, 1.00f));
        c[ImGuiCol_ChildBg] = srgb(ImVec4(0.190f, 0.190f, 0.190f, 1.00f));
        c[ImGuiCol_PopupBg] = srgb(ImVec4(0.070f, 0.070f, 0.070f, 1.00f));
        c[ImGuiCol_Border] = srgb(ImVec4(0.300f, 0.300f, 0.285f, 1.00f));
        c[ImGuiCol_BorderShadow] = srgb(ImVec4(0.000f, 0.000f, 0.000f, 0.00f));

        c[ImGuiCol_FrameBg] = srgb(ImVec4(0.070f, 0.070f, 0.070f, 1.00f));
        c[ImGuiCol_FrameBgHovered] = srgb(ImVec4(0.105f, 0.105f, 0.105f, 1.00f));
        c[ImGuiCol_FrameBgActive] = srgb(ImVec4(0.120f, 0.320f, 0.500f, 1.00f));

        c[ImGuiCol_TitleBg] = srgb(ImVec4(0.020f, 0.020f, 0.020f, 1.00f));
        c[ImGuiCol_TitleBgActive] = srgb(ImVec4(0.020f, 0.020f, 0.020f, 1.00f));
        c[ImGuiCol_TitleBgCollapsed] = srgb(ImVec4(0.020f, 0.020f, 0.020f, 1.00f));
        c[ImGuiCol_MenuBarBg] = srgb(ImVec4(0.020f, 0.020f, 0.020f, 1.00f));

        c[ImGuiCol_ScrollbarBg] = srgb(ImVec4(0.135f, 0.135f, 0.135f, 1.00f));
        c[ImGuiCol_ScrollbarGrab] = srgb(ImVec4(0.400f, 0.400f, 0.385f, 0.75f));
        c[ImGuiCol_ScrollbarGrabHovered] = srgb(ImVec4(0.470f, 0.470f, 0.455f, 0.85f));
        c[ImGuiCol_ScrollbarGrabActive] = srgb(ImVec4(0.550f, 0.550f, 0.535f, 1.00f));

        c[ImGuiCol_CheckMark] = srgb(ImVec4(0.260f, 0.610f, 0.900f, 1.00f));
        c[ImGuiCol_SliderGrab] = srgb(ImVec4(0.260f, 0.610f, 0.900f, 1.00f));
        c[ImGuiCol_SliderGrabActive] = srgb(ImVec4(0.370f, 0.710f, 1.000f, 1.00f));

        c[ImGuiCol_Button] = srgb(ImVec4(0.280f, 0.280f, 0.270f, 1.00f));
        c[ImGuiCol_ButtonHovered] = srgb(ImVec4(0.340f, 0.340f, 0.325f, 1.00f));
        c[ImGuiCol_ButtonActive] = srgb(ImVec4(0.120f, 0.320f, 0.500f, 1.00f));

        c[ImGuiCol_Header] = srgb(ImVec4(0.300f, 0.300f, 0.290f, 1.00f));
        c[ImGuiCol_HeaderHovered] = srgb(ImVec4(0.360f, 0.360f, 0.345f, 1.00f));
        c[ImGuiCol_HeaderActive] = srgb(ImVec4(0.100f, 0.290f, 0.460f, 1.00f));

        c[ImGuiCol_Separator] = srgb(ImVec4(0.330f, 0.330f, 0.310f, 1.00f));
        c[ImGuiCol_SeparatorHovered] = srgb(ImVec4(0.260f, 0.610f, 0.900f, 1.00f));
        c[ImGuiCol_SeparatorActive] = srgb(ImVec4(0.260f, 0.610f, 0.900f, 1.00f));
        c[ImGuiCol_ResizeGrip] = srgb(ImVec4(0.260f, 0.610f, 0.900f, 0.35f));
        c[ImGuiCol_ResizeGripHovered] = srgb(ImVec4(0.260f, 0.610f, 0.900f, 0.70f));
        c[ImGuiCol_ResizeGripActive] = srgb(ImVec4(0.260f, 0.610f, 0.900f, 1.00f));

        c[ImGuiCol_Tab] = srgb(ImVec4(0.260f, 0.260f, 0.250f, 1.00f));
        c[ImGuiCol_TabHovered] = srgb(ImVec4(0.350f, 0.350f, 0.335f, 1.00f));
        c[ImGuiCol_TabActive] = srgb(ImVec4(0.300f, 0.300f, 0.290f, 1.00f));
        c[ImGuiCol_TabUnfocused] = srgb(ImVec4(0.210f, 0.210f, 0.200f, 1.00f));
        c[ImGuiCol_TabUnfocusedActive] = srgb(ImVec4(0.260f, 0.260f, 0.250f, 1.00f));

        c[ImGuiCol_DockingPreview] = srgb(ImVec4(0.260f, 0.610f, 0.900f, 0.45f));
        c[ImGuiCol_DockingEmptyBg] = srgb(ImVec4(0.190f, 0.190f, 0.190f, 1.00f));
        c[ImGuiCol_PlotLines] = srgb(ImVec4(0.530f, 0.700f, 0.860f, 1.00f));
        c[ImGuiCol_PlotLinesHovered] = srgb(ImVec4(0.260f, 0.610f, 0.900f, 1.00f));
        c[ImGuiCol_PlotHistogram] = srgb(ImVec4(0.260f, 0.610f, 0.900f, 1.00f));
        c[ImGuiCol_PlotHistogramHovered] = srgb(ImVec4(0.370f, 0.710f, 1.000f, 1.00f));
        c[ImGuiCol_TableHeaderBg] = srgb(ImVec4(0.300f, 0.300f, 0.290f, 1.00f));
        c[ImGuiCol_TableBorderStrong] = srgb(ImVec4(0.330f, 0.330f, 0.310f, 1.00f));
        c[ImGuiCol_TableBorderLight] = srgb(ImVec4(0.260f, 0.260f, 0.250f, 1.00f));
        c[ImGuiCol_TableRowBg] = srgb(ImVec4(0.000f, 0.000f, 0.000f, 0.00f));
        c[ImGuiCol_TableRowBgAlt] = srgb(ImVec4(1.000f, 1.000f, 1.000f, 0.03f));
        c[ImGuiCol_TextSelectedBg] = srgb(ImVec4(0.150f, 0.390f, 0.610f, 0.65f));
        c[ImGuiCol_DragDropTarget] = srgb(ImVec4(0.260f, 0.610f, 0.900f, 0.90f));
        c[ImGuiCol_NavHighlight] = srgb(ImVec4(0.260f, 0.610f, 0.900f, 1.00f));
        c[ImGuiCol_ModalWindowDimBg] = srgb(ImVec4(0.000f, 0.000f, 0.000f, 0.55f));
    }

#else
    ImGuiLayer::ImGuiLayer(Device &, Window &, VkRenderPass, uint32_t) {
    }
    ImGuiLayer::~ImGuiLayer() = default;
    void ImGuiLayer::beginFrame(bool) {
    }
    void ImGuiLayer::endFrame() {
    }
    void ImGuiLayer::renderDrawData(VkCommandBuffer) {
    }
    VkDescriptorSet ImGuiLayer::addTexture(VkImageView, VkImageLayout) { return VK_NULL_HANDLE; }
    VkDescriptorSet ImGuiLayer::addTexture(VkSampler, VkImageView, VkImageLayout) { return VK_NULL_HANDLE; }
    void ImGuiLayer::removeTexture(VkDescriptorSet) {
    }
#endif
}
