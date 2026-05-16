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

    static constexpr float TITLEBAR_HEIGHT         = 33.0f;

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
        initInfo.Instance                      = device.getInstance();
        initInfo.PhysicalDevice                = device.getPhysicalDevice();
        initInfo.Device                        = device.device();
        initInfo.QueueFamily                   = device.findPhysicalQueueFamilies().graphicsFamily.value();
        initInfo.Queue                         = device.graphicsQueue();
        initInfo.DescriptorPool                = descriptorPool;
        initInfo.MinImageCount                 = 2;
        initInfo.ImageCount                    = imageCount;
        initInfo.UseDynamicRendering           = false;
        initInfo.PipelineInfoMain.RenderPass   = renderPass;
        initInfo.PipelineInfoMain.Subpass      = 0;
        initInfo.PipelineInfoMain.MSAASamples  = VK_SAMPLE_COUNT_1_BIT;

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
        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + TITLEBAR_HEIGHT));
        ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, viewport->Size.y - TITLEBAR_HEIGHT));
        ImGui::SetNextWindowViewport(viewport->ID);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

        constexpr ImGuiWindowFlags hostFlags =
            ImGuiWindowFlags_NoTitleBar          |
            ImGuiWindowFlags_NoCollapse          |
            ImGuiWindowFlags_NoResize            |
            ImGuiWindowFlags_NoMove              |
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

    VkDescriptorSet ImGuiLayer::addTexture(VkSampler sampler, VkImageView imageView,
                                            VkImageLayout imageLayout) {
        return ImGui_ImplVulkan_AddTexture(sampler, imageView, imageLayout);
    }

    void ImGuiLayer::removeTexture(VkDescriptorSet texture) {
        ImGui_ImplVulkan_RemoveTexture(texture);
    }

    void ImGuiLayer::createDescriptorPool(Device &device) {
        const std::array<VkDescriptorPoolSize, 3> poolSizes{{
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         1000},
        }};

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets       = 1000;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes    = poolSizes.data();

        if (vkCreateDescriptorPool(device.device(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS)
            throw std::runtime_error("Failed to create ImGui descriptor pool");
    }

    void ImGuiLayer::setStyle() {
        ImGuiStyle &style = ImGui::GetStyle();
        style.WindowRounding   = 0.0f;
        style.ChildRounding    = 0.0f;
        style.PopupRounding    = 2.0f;
        style.FrameRounding    = 2.0f;
        style.GrabRounding     = 2.0f;
        style.TabRounding      = 2.0f;
        style.ScrollbarRounding= 2.0f;
        style.WindowBorderSize = 1.0f;
        style.FrameBorderSize  = 0.0f;
        style.TabBorderSize    = 0.0f;
        style.WindowPadding    = ImVec2(8.0f, 6.0f);
        style.FramePadding     = ImVec2(7.0f, 4.0f);
        style.ItemSpacing      = ImVec2(6.0f, 4.0f);
        style.ScrollbarSize    = 12.0f;

        // Palette constants
        // #202020 = 32/255  ≈ 0.125   (titlebar / menu bar)
        // #1a1a1a = 26/255  ≈ 0.102   (window / child bg  — same as before but kept)
        // #181818 = 24/255  ≈ 0.094   (docking empty, tab active)
        // #252525 = 37/255  ≈ 0.145   (frame bg)
        // #2c2c2c = 44/255  ≈ 0.173   (frame hovered, button hovered)
        // Text pulled to a cooler mid-gray instead of near-white

        ImVec4 *c = style.Colors;
        c[ImGuiCol_Text]                  = ImVec4(0.660f, 0.660f, 0.660f, 1.00f); // softer gray, less glaring
        c[ImGuiCol_TextDisabled]          = ImVec4(0.310f, 0.310f, 0.310f, 1.00f);
        c[ImGuiCol_WindowBg]              = ImVec4(0.102f, 0.102f, 0.102f, 1.00f); // #1a1a1a
        c[ImGuiCol_ChildBg]               = ImVec4(0.102f, 0.102f, 0.102f, 1.00f); // #1a1a1a
        c[ImGuiCol_PopupBg]               = ImVec4(32.0f / 255.0f, 32.0f / 255.0f, 32.0f / 255.0f, 1.00f); // #202020
        c[ImGuiCol_Border]                = ImVec4(0.145f, 0.145f, 0.145f, 1.00f); // subtler border
        c[ImGuiCol_BorderShadow]          = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);

        c[ImGuiCol_FrameBg]               = ImVec4(0.145f, 0.145f, 0.145f, 1.00f); // #252525
        c[ImGuiCol_FrameBgHovered]        = ImVec4(0.173f, 0.173f, 0.173f, 1.00f); // #2c2c2c
        c[ImGuiCol_FrameBgActive]         = ImVec4(0.000f, 0.400f, 0.650f, 1.00f);

        c[ImGuiCol_TitleBg]               = ImVec4(32.0f / 255.0f, 32.0f / 255.0f, 32.0f / 255.0f, 1.00f); // #202020
        c[ImGuiCol_TitleBgActive]         = ImVec4(32.0f / 255.0f, 32.0f / 255.0f, 32.0f / 255.0f, 1.00f); // #202020
        c[ImGuiCol_TitleBgCollapsed]      = ImVec4(32.0f / 255.0f, 32.0f / 255.0f, 32.0f / 255.0f, 1.00f); // #202020
        c[ImGuiCol_MenuBarBg]             = ImVec4(32.0f / 255.0f, 32.0f / 255.0f, 32.0f / 255.0f, 1.00f); // #202020

        c[ImGuiCol_ScrollbarBg]           = ImVec4(0.102f, 0.102f, 0.102f, 1.00f);
        c[ImGuiCol_ScrollbarGrab]         = ImVec4(0.220f, 0.220f, 0.220f, 0.55f);
        c[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.300f, 0.300f, 0.300f, 0.70f);
        c[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.380f, 0.380f, 0.380f, 0.85f);

        c[ImGuiCol_CheckMark]             = ImVec4(0.216f, 0.588f, 0.839f, 1.00f); // #3796d6
        c[ImGuiCol_SliderGrab]            = ImVec4(0.216f, 0.588f, 0.839f, 1.00f);
        c[ImGuiCol_SliderGrabActive]      = ImVec4(0.306f, 0.686f, 0.949f, 1.00f);

        c[ImGuiCol_Button]                = ImVec4(0.145f, 0.145f, 0.145f, 1.00f); // #252525
        c[ImGuiCol_ButtonHovered]         = ImVec4(0.173f, 0.173f, 0.173f, 1.00f); // #2c2c2c
        c[ImGuiCol_ButtonActive]          = ImVec4(0.000f, 0.400f, 0.650f, 1.00f);

        c[ImGuiCol_Header]                = ImVec4(0.000f, 0.330f, 0.540f, 0.65f);
        c[ImGuiCol_HeaderHovered]         = ImVec4(0.000f, 0.400f, 0.650f, 0.80f);
        c[ImGuiCol_HeaderActive]          = ImVec4(0.000f, 0.480f, 0.800f, 0.90f);

        c[ImGuiCol_Separator]             = ImVec4(0.145f, 0.145f, 0.145f, 1.00f);
        c[ImGuiCol_SeparatorHovered]      = ImVec4(0.043f, 0.361f, 0.678f, 1.00f);
        c[ImGuiCol_SeparatorActive]       = ImVec4(0.000f, 0.478f, 0.800f, 1.00f); // #007acc
        c[ImGuiCol_ResizeGrip]            = ImVec4(0.200f, 0.200f, 0.200f, 0.35f);
        c[ImGuiCol_ResizeGripHovered]     = ImVec4(0.000f, 0.478f, 0.800f, 0.70f);
        c[ImGuiCol_ResizeGripActive]      = ImVec4(0.000f, 0.478f, 0.800f, 1.00f);

        c[ImGuiCol_Tab]                   = ImVec4(32.0f / 255.0f, 32.0f / 255.0f, 32.0f / 255.0f, 1.00f); // #202020
        c[ImGuiCol_TabHovered]            = ImVec4(0.173f, 0.173f, 0.173f, 1.00f); // #2c2c2c
        c[ImGuiCol_TabActive]             = ImVec4(0.094f, 0.094f, 0.094f, 1.00f); // #181818
        c[ImGuiCol_TabUnfocused]          = ImVec4(0.102f, 0.102f, 0.102f, 1.00f); // #1a1a1a
        c[ImGuiCol_TabUnfocusedActive]    = ImVec4(32.0f / 255.0f, 32.0f / 255.0f, 32.0f / 255.0f, 1.00f); // #202020

        c[ImGuiCol_DockingPreview]        = ImVec4(0.000f, 0.478f, 0.800f, 0.45f);
        c[ImGuiCol_DockingEmptyBg]        = ImVec4(0.094f, 0.094f, 0.094f, 1.00f); // #181818
        c[ImGuiCol_PlotLines]             = ImVec4(0.420f, 0.620f, 0.800f, 1.00f);
        c[ImGuiCol_PlotLinesHovered]      = ImVec4(0.000f, 0.478f, 0.800f, 1.00f);
        c[ImGuiCol_PlotHistogram]         = ImVec4(0.000f, 0.478f, 0.800f, 1.00f);
        c[ImGuiCol_PlotHistogramHovered]  = ImVec4(0.216f, 0.588f, 0.839f, 1.00f);
        c[ImGuiCol_TableHeaderBg]         = ImVec4(0.145f, 0.145f, 0.145f, 1.00f); // #252525
        c[ImGuiCol_TableBorderStrong]     = ImVec4(0.173f, 0.173f, 0.173f, 1.00f);
        c[ImGuiCol_TableBorderLight]      = ImVec4(0.145f, 0.145f, 0.145f, 1.00f);
        c[ImGuiCol_TableRowBg]            = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
        c[ImGuiCol_TableRowBgAlt]         = ImVec4(1.000f, 1.000f, 1.000f, 0.02f);
        c[ImGuiCol_TextSelectedBg]        = ImVec4(0.043f, 0.361f, 0.678f, 0.55f);
        c[ImGuiCol_DragDropTarget]        = ImVec4(0.000f, 0.478f, 0.800f, 0.90f);
        c[ImGuiCol_NavHighlight]          = ImVec4(0.000f, 0.478f, 0.800f, 1.00f);
        c[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.000f, 0.000f, 0.000f, 0.55f);
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