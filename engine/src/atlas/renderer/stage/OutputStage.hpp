#pragma once

#include "IRenderStage.hpp"
#include "renderer/Device.hpp"
#include "renderer/Renderer.hpp"

namespace Atlas {
    class OutputStage : public IRenderStage {
    public:
        OutputStage(Device &device, Renderer &renderer);
        ~OutputStage() override = default;

        OutputStage(const OutputStage &) = delete;
        OutputStage &operator=(const OutputStage &) = delete;

        void getDeclaredOutputs(std::vector<Resource::Description> &out) const override;
        void getDeclaredInputs(std::vector<std::string> &out) const override;
        void onResourcesCreated(const Context &ctx) override;

        void record(VkCommandBuffer cmd, VkDescriptorSet globalSet) override;

    private:
        bool isTextureTarget() const;
        void recordToSwapChain(VkCommandBuffer cmd);
        void recordToTexture(VkCommandBuffer cmd);

        Device &device;
        Renderer &renderer;

        const GPUImage *postColorSource = nullptr;
        VkImageLayout sourceLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkImageLayout restoreLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    };
} // namespace Atlas
