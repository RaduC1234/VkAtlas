#pragma once

#include "IRenderStage.hpp"
#include "renderer/Device.hpp"
#include "renderer/abstraction/Pipeline.hpp"
#include "renderer/abstraction/Descriptors.hpp"
#include "renderer/abstraction/GPUImage.hpp"

namespace Atlas {
    class PostProcessPass : public IRenderStage {
    public:
        PostProcessPass(Device &device, const DescriptorSetLayout &globalSetLayout);
        ~PostProcessPass() override;

        PostProcessPass(const PostProcessPass &) = delete;
        PostProcessPass &operator=(const PostProcessPass &) = delete;

        void getDeclaredOutputs(std::vector<Resource::Description> &out) const override;
        void getDeclaredInputs(std::vector<std::string> &out) const override;
        void onResourcesCreated(const Context &ctx) override;

        void record(VkCommandBuffer cmd, VkDescriptorSet globalSet) override;

    private:
        Device &device;
        const DescriptorSetLayout &globalSetLayout;

        // Owned render pass + framebuffer targeting post_color.
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkExtent2D extent = {};
        const GPUImage *postColorTarget = nullptr;

        std::unique_ptr<Pipeline> pipeline;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        std::unique_ptr<DescriptorPool> pool;
        std::unique_ptr<DescriptorSetLayout> inputSetLayout;
        VkDescriptorSet inputSet = VK_NULL_HANDLE;
        VkSampler colorSampler = VK_NULL_HANDLE;
        VkSampler stencilSampler = VK_NULL_HANDLE;

        void createSampler();
        void createRenderPass(VkFormat colorFmt);
        void createFramebuffer(const GPUImage &colorImage);
        void createDescriptors(const GPUImage &colorImage, const GPUImage &depthImage);
        void createPipelineLayout();
        void createPipeline();
    };
} // namespace Atlas
