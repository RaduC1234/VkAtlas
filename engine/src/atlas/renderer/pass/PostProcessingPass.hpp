#pragma once
#include "IRenderPass.hpp"
#include "renderer/Device.hpp"
#include "renderer/Pipeline.hpp"
#include "renderer/Descriptors.hpp"

namespace Atlas {
    class PostProcessPass : public IRenderPass {
    public:
        // swapchainRenderPass  — the existing Renderer swapchain renderpass
        // geometryColorView    — HDR output from GeometryPass
        PostProcessPass(Device &device, VkRenderPass swapchainRenderPass, VkImageView geometryColorView, const DescriptorSetLayout& globalSetLayout);
        ~PostProcessPass() override;

        PostProcessPass(const PostProcessPass &) = delete;
        PostProcessPass &operator=(const PostProcessPass &) = delete;

        // IRenderPass — post process runs inside the already-open swapchain pass
        // so begin/end/barrier are no-ops; only record() is used
        void begin(VkCommandBuffer cmd) override {}
        void end(VkCommandBuffer cmd) override {}
        void barrier(VkCommandBuffer cmd) override {}
        void getDeclaredResources(std::vector<PassResource> &out) const override {}

        // call inside the open swapchain renderpass
        void record(VkCommandBuffer cmd, VkDescriptorSet globalSet);

    private:
        std::unique_ptr<Pipeline> pipeline;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

        std::unique_ptr<DescriptorPool> pool;

        std::unique_ptr<DescriptorSetLayout> inputSetLayout;
        VkDescriptorSet inputSet = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;

        Device &device;

        void createSampler();
        void createDescriptors(VkImageView geometryColorView);
        void createPipelineLayout(const DescriptorSetLayout &globalSetLayout);
        void createPipeline(VkRenderPass swapChainRenderPass);
    };
} // Atlas
