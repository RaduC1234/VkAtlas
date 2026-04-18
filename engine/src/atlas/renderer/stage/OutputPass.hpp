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

        void getDeclaredOutputs(std::vector<Resource::Description> &out) const override;
        void getDeclaredInputs(std::vector<std::string> &out) const override;
        void onResourcesCreated(const std::unordered_map<std::string, std::reference_wrapper<Resource> > &resources) override;

        void record(VkCommandBuffer cmd, VkDescriptorSet globalSet) override;

    private:
        Device &device;
        Renderer &renderer;

        // Non-owning; lifetime managed by RenderGraph.
        const GPUImage *postColorSource = nullptr;
    };
} // namespace Atlas
