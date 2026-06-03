#include "ImGuiLayer.hpp"

#include <array>
#include <stdexcept>

#include "asset/AssetManager.hpp"
#include "core/Profiler.hpp"
#include "core/Window.hpp"
#include "renderer/resources/GPUTexture.hpp"
#include "ui/theme/EditorTheme.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

namespace Atlas {
    ImGuiLayer::ImGuiLayer(Device &device, Window &window, VkRenderPass renderPass, uint32_t imageCount) : device(device.device()), atlasDevice(&device), nativeWindow(window.getNativeHandle()) {
        ATLAS_PROFILE_SCOPE("ImGuiLayer::create");
        createDescriptorPool(device);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        const std::string fontPath = AssetManager::resolveFilePath("##editor/fonts/Roboto-Medium.ttf").string();
        io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 15.0f);

        Editor::EditorTheme::apply(
            window.getTheme() == Window::Theme::Light
                ? Editor::EditorTheme::Mode::Light
                : Editor::EditorTheme::Mode::Dark);

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
        ATLAS_PROFILE_SCOPE("ImGuiLayer::destroy");
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
        ATLAS_PROFILE_SCOPE("ImGuiLayer::beginFrame");
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
        ATLAS_PROFILE_SCOPE("ImGuiLayer::endFrame");
        ImGui::Render();
    }

    void ImGuiLayer::render(VkCommandBuffer commandBuffer) {
        ATLAS_PROFILE_SCOPE("ImGuiLayer::render");
        ATLAS_PROFILE_GPU_ZONE(atlasDevice->gpuProfilerContext(), commandBuffer, "Editor::ImGuiDrawData");
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
    }

    VkDescriptorSet ImGuiLayer::addTexture(VkImageView imageView, VkImageLayout imageLayout) {
        return addTexture(IGPUResource::default_<GPUTexture>().descriptor().sampler, imageView, imageLayout);
    }

    VkDescriptorSet ImGuiLayer::addTexture(VkSampler sampler, VkImageView imageView, VkImageLayout imageLayout) {
        if (imageView == VK_NULL_HANDLE || imageLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
            return VK_NULL_HANDLE;
        }

        return ImGui_ImplVulkan_AddTexture(sampler, imageView, imageLayout);
    }

    void ImGuiLayer::removeTexture(VkDescriptorSet texture) {
        if (texture == VK_NULL_HANDLE) {
            return;
        }

        ImGui_ImplVulkan_RemoveTexture(texture);
    }

    void ImGuiLayer::createDescriptorPool(Device &device) {
        ATLAS_PROFILE_SCOPE("ImGuiLayer::createDescriptorPool");
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

}
