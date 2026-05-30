#pragma once

#include "Device.hpp"
#include "core/Window.hpp"

#include <vulkan/vulkan.h>

#include <imgui.h>

namespace Atlas {
    class ImGuiLayer {
    public:
        ImGuiLayer(Device &device, Window &window, VkRenderPass renderPass, uint32_t imageCount);
        ~ImGuiLayer();

        ImGuiLayer(const ImGuiLayer &) = delete;
        ImGuiLayer &operator=(const ImGuiLayer &) = delete;

        void beginFrame(bool createDockSpace);
        void endFrame();
        void render(VkCommandBuffer commandBuffer);

        static VkDescriptorSet addTexture(VkImageView imageView, VkImageLayout imageLayout);
        static VkDescriptorSet addTexture(VkSampler sampler, VkImageView imageView, VkImageLayout imageLayout);
        static void removeTexture(VkDescriptorSet texture);

    private:
        void createDescriptorPool(Device &device);
        void setStyleWhite();
        void setStyleDark();

        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        void *nativeWindow = nullptr;
    };
}
