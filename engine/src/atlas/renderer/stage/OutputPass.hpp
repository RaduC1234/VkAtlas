#pragma once

#include "IRenderStage.hpp"
#include "renderer/Device.hpp"
#include "renderer/Renderer.hpp"

namespace Atlas {
    class OutputPass : public IRenderStage {
    public:
        OutputPass(Device &device, Renderer &renderer);
        ~OutputPass() override = default;

        OutputPass(const OutputPass &) = delete;
        OutputPass &operator=(const OutputPass &) = delete;

        void getDeclaredOutputs(std::vector<Resource> &out) const override {
            // Writes directly to the swapchain — no owned resource declared.
        }

        void getDeclaredInputs(std::vector<std::string> &out) const override {
            out.push_back("post_color");
        }

        void onResourcesCreated(
            const std::unordered_map<std::string, std::reference_wrapper<GPUImage>> &resources) override;

        void record(VkCommandBuffer cmd, VkDescriptorSet globalSet) override;

    private:
        Device   &device;
        Renderer &renderer;

        // Non-owning; lifetime managed by RenderGraph.
        const GPUImage *postColorSource = nullptr;
    };
} // namespace Atlas