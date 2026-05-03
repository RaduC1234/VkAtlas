#pragma once
#include "Device.hpp"
#include "core/Window.hpp"

namespace Atlas {
    class ImGuiLayer {
        public:
        ImGuiLayer(Device& device, Window& window, VkRenderPass renderPass, uint32_t imageCount);
        ~ImGuiLayer();

        void beginFrame(bool createDockSpace);
        void endFrame(VkCommandBuffer commandBuffer);

        static VkDescriptorSet addTexture(VkSampler sampler, VkImageView imageView, VkImageLayout imageLayout);
    private:
        void createDescriptorPool(Device& device);
        void setStyle();

        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
    };
}
