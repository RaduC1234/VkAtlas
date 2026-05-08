#pragma once

#include "IRenderStage.hpp"
#include "renderer/Device.hpp"
#include "renderer/Renderer.hpp"

namespace Atlas {
    class OutputStage : public IRenderStage {
    public:
        OutputStage(Device &device, Renderer &renderer);
        ~OutputStage() override;

        OutputStage(const OutputStage &) = delete;
        OutputStage &operator=(const OutputStage &) = delete;

        void getDeclaredOutputs(std::vector<Resource::Description> &out) const override;
        void getDeclaredInputs(std::vector<std::string> &out) const override;
        void onResourcesCreated(const Context &ctx) override;

        void record(VkCommandBuffer cmd, VkDescriptorSet globalSet) override;

    private:
        bool isImGuiTarget() const;
        void recordToSwapChain(VkCommandBuffer cmd);
        void recordToViewport(VkCommandBuffer cmd);

        Device &device;
        Renderer &renderer;

        const GPUImage *postColorSource = nullptr;
        VkImageLayout sourceLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkImageLayout restoreLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VkSampler viewportSampler = VK_NULL_HANDLE;
        VkDescriptorSet viewportTexture = VK_NULL_HANDLE;
    };
} // namespace Atlas
