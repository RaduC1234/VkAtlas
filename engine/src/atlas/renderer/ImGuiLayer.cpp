#include "ImGuiLayer.hpp"

#include <array>
#include <stdexcept>

#include "core/Window.hpp"

#if defined(ATLAS_PLATFORM_DESKTOP)
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#endif

namespace Atlas {
    static constexpr ImVec4 srgb(ImVec4 c) {
        auto toLinear = [](float x) {
            return x <= 0.04045f ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f);
        };
        return ImVec4(toLinear(c.x), toLinear(c.y), toLinear(c.z), c.w);
    }

#if defined(ATLAS_PLATFORM_DESKTOP)
    ImGuiLayer::ImGuiLayer(Device &device, Window &window, VkRenderPass renderPass, uint32_t imageCount) : device(device.device()), nativeWindow(window.getNativeHandle()) {
        createDescriptorPool(device);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.Fonts->AddFontFromFileTTF("assets/engine/Roboto-Medium.ttf", 15.0f);

        if (window.getTheme() == Window::Theme::Light) {
            setStyleWhite();
        } else {
            setStyleDark();
        }

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

    void ImGuiLayer::render(VkCommandBuffer commandBuffer) {
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

    void ImGuiLayer::setStyleWhite() {
        ImGuiStyle &style = ImGui::GetStyle();

        // -- Shape --
        style.WindowRounding = 6.0f;
        style.ChildRounding = 4.0f;
        style.PopupRounding = 6.0f;
        style.FrameRounding = 4.0f;
        style.GrabRounding = 4.0f;
        style.TabRounding = 4.0f;
        style.ScrollbarRounding = 4.0f;

        // -- Borders --
        style.WindowBorderSize = 0.5f;
        style.FrameBorderSize = 0.5f;
        style.TabBorderSize = 0.0f;
        style.PopupBorderSize = 0.5f;

        // -- Spacing --
        style.WindowPadding = ImVec2(12.0f, 10.0f);
        style.FramePadding = ImVec2(8.0f, 4.0f);
        style.ItemSpacing = ImVec2(8.0f, 5.0f);
        style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
        style.IndentSpacing = 18.0f;
        style.ScrollbarSize = 8.0f;
        style.GrabMinSize = 8.0f;

        // -- Colors --
        // Light neutral surface palette, matches the mockup.
        // All values are sRGB; pass through your srgb() linearise helper.
        ImVec4 *c = style.Colors;

        // Text
        c[ImGuiCol_Text] = srgb(ImVec4(0.100f, 0.100f, 0.100f, 1.00f)); // near-black
        c[ImGuiCol_TextDisabled] = srgb(ImVec4(0.580f, 0.580f, 0.580f, 1.00f));

        // Backgrounds
        c[ImGuiCol_WindowBg] = srgb(ImVec4(0.960f, 0.960f, 0.960f, 1.00f)); // primary surface
        c[ImGuiCol_ChildBg] = srgb(ImVec4(0.960f, 0.960f, 0.960f, 1.00f));
        c[ImGuiCol_PopupBg] = srgb(ImVec4(0.980f, 0.980f, 0.980f, 1.00f));

        // Borders — very subtle, like 0.5px lines
        c[ImGuiCol_Border] = srgb(ImVec4(0.800f, 0.800f, 0.800f, 0.60f));
        c[ImGuiCol_BorderShadow] = srgb(ImVec4(0.000f, 0.000f, 0.000f, 0.00f));

        // Frame (inputs, combos, etc.)
        c[ImGuiCol_FrameBg] = srgb(ImVec4(0.920f, 0.920f, 0.920f, 1.00f));
        c[ImGuiCol_FrameBgHovered] = srgb(ImVec4(0.880f, 0.888f, 0.900f, 1.00f));
        c[ImGuiCol_FrameBgActive] = srgb(ImVec4(0.840f, 0.870f, 0.920f, 1.00f));

        // Title bar — same as window, no contrast stripe
        c[ImGuiCol_TitleBg] = srgb(ImVec4(0.930f, 0.930f, 0.930f, 1.00f));
        c[ImGuiCol_TitleBgActive] = srgb(ImVec4(0.930f, 0.930f, 0.930f, 1.00f));
        c[ImGuiCol_TitleBgCollapsed] = srgb(ImVec4(0.930f, 0.930f, 0.930f, 1.00f));
        c[ImGuiCol_MenuBarBg] = srgb(ImVec4(0.980f, 0.980f, 0.980f, 1.00f));

        // Scrollbar
        c[ImGuiCol_ScrollbarBg] = srgb(ImVec4(0.940f, 0.940f, 0.940f, 1.00f));
        c[ImGuiCol_ScrollbarGrab] = srgb(ImVec4(0.720f, 0.720f, 0.720f, 1.00f));
        c[ImGuiCol_ScrollbarGrabHovered] = srgb(ImVec4(0.620f, 0.620f, 0.620f, 1.00f));
        c[ImGuiCol_ScrollbarGrabActive] = srgb(ImVec4(0.500f, 0.500f, 0.500f, 1.00f));

        // Accent — single blue used for all interactive highlights
        // matches the play-button / axis accent in the mockup
        const ImVec4 accent = srgb(ImVec4(0.220f, 0.530f, 0.860f, 1.00f));
        const ImVec4 accentHover = srgb(ImVec4(0.280f, 0.590f, 0.920f, 1.00f));
        const ImVec4 accentDim = srgb(ImVec4(0.220f, 0.530f, 0.860f, 0.35f));

        c[ImGuiCol_CheckMark] = accent;
        c[ImGuiCol_SliderGrab] = accent;
        c[ImGuiCol_SliderGrabActive] = accentHover;

        // Buttons — neutral surface, accent on press
        c[ImGuiCol_Button] = srgb(ImVec4(0.900f, 0.900f, 0.900f, 1.00f));
        c[ImGuiCol_ButtonHovered] = srgb(ImVec4(0.860f, 0.870f, 0.890f, 1.00f));
        c[ImGuiCol_ButtonActive] = srgb(ImVec4(0.820f, 0.850f, 0.920f, 1.00f));

        // Headers (tree nodes, selectables, collapsing headers)
        c[ImGuiCol_Header] = srgb(ImVec4(0.900f, 0.900f, 0.900f, 1.00f));
        c[ImGuiCol_HeaderHovered] = srgb(ImVec4(0.870f, 0.878f, 0.895f, 1.00f));
        c[ImGuiCol_HeaderActive] = srgb(ImVec4(0.820f, 0.850f, 0.920f, 1.00f));

        // Separator
        c[ImGuiCol_Separator] = srgb(ImVec4(0.800f, 0.800f, 0.800f, 0.60f));
        c[ImGuiCol_SeparatorHovered] = accent;
        c[ImGuiCol_SeparatorActive] = accent;

        // Resize grip
        c[ImGuiCol_ResizeGrip] = accentDim;
        c[ImGuiCol_ResizeGripHovered] = accent;
        c[ImGuiCol_ResizeGripActive] = accentHover;

        // Tabs — panel tabs as in the bottom bar of the mockup
        c[ImGuiCol_Tab] = srgb(ImVec4(0.940f, 0.940f, 0.940f, 1.00f));
        c[ImGuiCol_TabHovered] = srgb(ImVec4(0.900f, 0.905f, 0.915f, 1.00f));
        c[ImGuiCol_TabActive] = srgb(ImVec4(0.960f, 0.960f, 0.960f, 1.00f)); // lifted = active
        c[ImGuiCol_TabUnfocused] = srgb(ImVec4(0.930f, 0.930f, 0.930f, 1.00f));
        c[ImGuiCol_TabUnfocusedActive] = srgb(ImVec4(0.950f, 0.950f, 0.950f, 1.00f));

        // Docking
        c[ImGuiCol_DockingPreview] = srgb(ImVec4(0.220f, 0.530f, 0.860f, 0.30f));
        c[ImGuiCol_DockingEmptyBg] = srgb(ImVec4(0.920f, 0.920f, 0.920f, 1.00f));

        // Plots
        c[ImGuiCol_PlotLines] = accent;
        c[ImGuiCol_PlotLinesHovered] = accentHover;
        c[ImGuiCol_PlotHistogram] = accent;
        c[ImGuiCol_PlotHistogramHovered] = accentHover;

        // Tables
        c[ImGuiCol_TableHeaderBg] = srgb(ImVec4(0.920f, 0.922f, 0.928f, 1.00f));
        c[ImGuiCol_TableBorderStrong] = srgb(ImVec4(0.780f, 0.780f, 0.780f, 1.00f));
        c[ImGuiCol_TableBorderLight] = srgb(ImVec4(0.860f, 0.860f, 0.860f, 1.00f));
        c[ImGuiCol_TableRowBg] = srgb(ImVec4(0.000f, 0.000f, 0.000f, 0.00f));
        c[ImGuiCol_TableRowBgAlt] = srgb(ImVec4(0.000f, 0.000f, 0.000f, 0.02f));

        // Misc
        c[ImGuiCol_TextSelectedBg] = srgb(ImVec4(0.220f, 0.530f, 0.860f, 0.30f));
        c[ImGuiCol_DragDropTarget] = accent;
        c[ImGuiCol_NavHighlight] = accent;
        c[ImGuiCol_ModalWindowDimBg] = srgb(ImVec4(0.100f, 0.100f, 0.100f, 0.40f));
    }

    void ImGuiLayer::setStyleDark() {
        ImGuiStyle &style = ImGui::GetStyle();

        // -- Shape --
        style.WindowRounding = 6.0f;
        style.ChildRounding = 4.0f;
        style.PopupRounding = 6.0f;
        style.FrameRounding = 4.0f;
        style.GrabRounding = 4.0f;
        style.TabRounding = 4.0f;
        style.ScrollbarRounding = 4.0f;

        // -- Borders --
        style.WindowBorderSize = 0.5f;
        style.FrameBorderSize = 0.5f;
        style.TabBorderSize = 0.0f;
        style.PopupBorderSize = 0.5f;

        // -- Spacing --
        style.WindowPadding = ImVec2(12.0f, 10.0f);
        style.FramePadding = ImVec2(8.0f, 4.0f);
        style.ItemSpacing = ImVec2(8.0f, 5.0f);
        style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
        style.IndentSpacing = 18.0f;
        style.ScrollbarSize = 8.0f;
        style.GrabMinSize = 8.0f;

        // -- Colors --
        // Dark neutral surface palette.
        // Three-level depth: 0.08 (deep bg) → 0.11 (panel) → 0.14 (raised frame)
        // Single blue accent throughout.
        ImVec4 *c = style.Colors;

        // Text
        c[ImGuiCol_Text] = srgb(ImVec4(0.880f, 0.880f, 0.880f, 1.00f));
        c[ImGuiCol_TextDisabled] = srgb(ImVec4(0.400f, 0.400f, 0.400f, 1.00f));

        // Backgrounds — three distinct levels
        c[ImGuiCol_WindowBg] = srgb(ImVec4(0.110f, 0.110f, 0.115f, 1.00f)); // panels
        c[ImGuiCol_ChildBg] = srgb(ImVec4(0.110f, 0.110f, 0.115f, 1.00f));
        c[ImGuiCol_PopupBg] = srgb(ImVec4(0.080f, 0.080f, 0.085f, 1.00f)); // deepest

        // Borders — barely visible hairlines
        c[ImGuiCol_Border] = srgb(ImVec4(1.000f, 1.000f, 1.000f, 0.07f));
        c[ImGuiCol_BorderShadow] = srgb(ImVec4(0.000f, 0.000f, 0.000f, 0.00f));

        // Frames (inputs, combos, sliders)
        c[ImGuiCol_FrameBg] = srgb(ImVec4(0.080f, 0.080f, 0.085f, 1.00f));
        c[ImGuiCol_FrameBgHovered] = srgb(ImVec4(0.140f, 0.140f, 0.148f, 1.00f));
        c[ImGuiCol_FrameBgActive] = srgb(ImVec4(0.160f, 0.200f, 0.260f, 1.00f));

        // Title bar — flush with window, no stripe
        c[ImGuiCol_TitleBg] = srgb(ImVec4(0.080f, 0.080f, 0.085f, 1.00f));
        c[ImGuiCol_TitleBgActive] = srgb(ImVec4(0.080f, 0.080f, 0.085f, 1.00f));
        c[ImGuiCol_TitleBgCollapsed] = srgb(ImVec4(0.080f, 0.080f, 0.085f, 1.00f));
        c[ImGuiCol_MenuBarBg] = srgb(ImVec4(0.090f, 0.090f, 0.095f, 1.00f));

        // Scrollbar
        c[ImGuiCol_ScrollbarBg] = srgb(ImVec4(0.090f, 0.090f, 0.095f, 1.00f));
        c[ImGuiCol_ScrollbarGrab] = srgb(ImVec4(0.280f, 0.280f, 0.290f, 1.00f));
        c[ImGuiCol_ScrollbarGrabHovered] = srgb(ImVec4(0.360f, 0.360f, 0.370f, 1.00f));
        c[ImGuiCol_ScrollbarGrabActive] = srgb(ImVec4(0.440f, 0.444f, 0.460f, 1.00f));

        // Accent — same blue family as light theme, slightly brighter for dark bg
        const ImVec4 accent = srgb(ImVec4(0.260f, 0.580f, 0.920f, 1.00f));
        const ImVec4 accentHover = srgb(ImVec4(0.360f, 0.660f, 1.000f, 1.00f));
        const ImVec4 accentDim = srgb(ImVec4(0.260f, 0.580f, 0.920f, 0.30f));

        c[ImGuiCol_CheckMark] = accent;
        c[ImGuiCol_SliderGrab] = accent;
        c[ImGuiCol_SliderGrabActive] = accentHover;

        // Buttons
        c[ImGuiCol_Button] = srgb(ImVec4(0.180f, 0.180f, 0.190f, 1.00f));
        c[ImGuiCol_ButtonHovered] = srgb(ImVec4(0.220f, 0.225f, 0.235f, 1.00f));
        c[ImGuiCol_ButtonActive] = srgb(ImVec4(0.160f, 0.220f, 0.310f, 1.00f));

        // Headers (tree nodes, selectables, collapsing headers)
        c[ImGuiCol_Header] = srgb(ImVec4(0.180f, 0.180f, 0.190f, 1.00f));
        c[ImGuiCol_HeaderHovered] = srgb(ImVec4(0.220f, 0.225f, 0.235f, 1.00f));
        c[ImGuiCol_HeaderActive] = srgb(ImVec4(0.160f, 0.220f, 0.310f, 1.00f));

        // Separator
        c[ImGuiCol_Separator] = srgb(ImVec4(1.000f, 1.000f, 1.000f, 0.07f));
        c[ImGuiCol_SeparatorHovered] = accent;
        c[ImGuiCol_SeparatorActive] = accent;

        // Resize grip
        c[ImGuiCol_ResizeGrip] = accentDim;
        c[ImGuiCol_ResizeGripHovered] = accent;
        c[ImGuiCol_ResizeGripActive] = accentHover;

        // Tabs
        c[ImGuiCol_Tab] = srgb(ImVec4(0.090f, 0.090f, 0.095f, 1.00f));
        c[ImGuiCol_TabHovered] = srgb(ImVec4(0.160f, 0.162f, 0.170f, 1.00f));
        c[ImGuiCol_TabActive] = srgb(ImVec4(0.110f, 0.110f, 0.115f, 1.00f)); // lifted = matches WindowBg
        c[ImGuiCol_TabUnfocused] = srgb(ImVec4(0.090f, 0.090f, 0.095f, 1.00f));
        c[ImGuiCol_TabUnfocusedActive] = srgb(ImVec4(0.100f, 0.100f, 0.105f, 1.00f));

        // Docking
        c[ImGuiCol_DockingPreview] = srgb(ImVec4(0.260f, 0.580f, 0.920f, 0.25f));
        c[ImGuiCol_DockingEmptyBg] = srgb(ImVec4(0.080f, 0.080f, 0.085f, 1.00f));

        // Plots
        c[ImGuiCol_PlotLines] = accent;
        c[ImGuiCol_PlotLinesHovered] = accentHover;
        c[ImGuiCol_PlotHistogram] = accent;
        c[ImGuiCol_PlotHistogramHovered] = accentHover;

        // Tables
        c[ImGuiCol_TableHeaderBg] = srgb(ImVec4(0.130f, 0.130f, 0.138f, 1.00f));
        c[ImGuiCol_TableBorderStrong] = srgb(ImVec4(1.000f, 1.000f, 1.000f, 0.10f));
        c[ImGuiCol_TableBorderLight] = srgb(ImVec4(1.000f, 1.000f, 1.000f, 0.05f));
        c[ImGuiCol_TableRowBg] = srgb(ImVec4(0.000f, 0.000f, 0.000f, 0.00f));
        c[ImGuiCol_TableRowBgAlt] = srgb(ImVec4(1.000f, 1.000f, 1.000f, 0.02f));

        // Misc
        c[ImGuiCol_TextSelectedBg] = srgb(ImVec4(0.260f, 0.580f, 0.920f, 0.30f));
        c[ImGuiCol_DragDropTarget] = accent;
        c[ImGuiCol_NavHighlight] = accent;
        c[ImGuiCol_ModalWindowDimBg] = srgb(ImVec4(0.000f, 0.000f, 0.000f, 0.50f));
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
