#pragma once

#include "Device.hpp"
#include "core/Window.hpp"

#include <vulkan/vulkan.h>

#include <imgui.h>

namespace Atlas {
    static ImVec4 srgb(ImVec4 c) {
        auto toLinear = [](float x) {
            return x <= 0.04045f ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f);
        };
        return ImVec4(toLinear(c.x), toLinear(c.y), toLinear(c.z), c.w);
    }

    class ImGuiLayer {
    public:
        ImGuiLayer(Device &device, Window &window, VkRenderPass renderPass, uint32_t imageCount);
        ~ImGuiLayer();

        ImGuiLayer(const ImGuiLayer &) = delete;
        ImGuiLayer &operator=(const ImGuiLayer &) = delete;

        void beginFrame(bool createDockSpace);
        void endFrame();
        void renderDrawData(VkCommandBuffer commandBuffer);

        static VkDescriptorSet addTexture(VkImageView imageView, VkImageLayout imageLayout);
        static VkDescriptorSet addTexture(VkSampler sampler, VkImageView imageView, VkImageLayout imageLayout);
        static void removeTexture(VkDescriptorSet texture);

    private:
        void createDescriptorPool(Device &device);
        void setStyle();

        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        void *nativeWindow = nullptr;
    };
}
