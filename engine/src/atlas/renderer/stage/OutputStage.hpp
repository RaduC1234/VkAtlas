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
        void onResourcesCreated(const std::unordered_map<std::string, std::reference_wrapper<Resource> > &resources) override;

        void record(VkCommandBuffer cmd, VkDescriptorSet globalSet) override;

    private:
        Device &device;
        Renderer &renderer;

        // Non-owning; lifetime managed by RenderGraph.
        const GPUImage *postColorSource = nullptr;
    };
} // namespace Atlas
