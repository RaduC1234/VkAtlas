#pragma once

#include "RenderStage.hpp"
#include "entity/Object.hpp"
#include "renderer/Device.hpp"
#include "renderer/abstraction/Pipeline.hpp"
#include "renderer/abstraction/Descriptors.hpp"
#include "renderer/abstraction/GPUImage.hpp"

#include <glm/vec4.hpp>

namespace Atlas {
    class PostProcessPass : public RenderStage {
    public:
        PostProcessPass(Device &device, const DescriptorSetLayout &globalSetLayout, bool bloomEnabled = false);
        ~PostProcessPass() override;

        PostProcessPass(const PostProcessPass &) = delete;
        PostProcessPass &operator=(const PostProcessPass &) = delete;

        void getDeclaredOutputs(std::vector<Resource::Description> &out) const override;
        void getDeclaredInputs(std::vector<std::string> &out) const override;
        void onResourcesCreated(const Context &ctx) override;

        void onUpdate(entt::registry &registry) override;

        void record(VkCommandBuffer cmd, VkDescriptorSet globalSet) override;

    private:
        Device &device;
        const DescriptorSetLayout &globalSetLayout;

        // Owned render pass + framebuffer targeting post_color.
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkExtent2D extent = {};
        const GPUImage *geometryColorSource = nullptr;
        const GPUImage *postColorTarget = nullptr;
        VkImageLayout geometryColorLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        bool postColorInitialized = false;

        std::unique_ptr<Pipeline> pipeline;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        std::unique_ptr<DescriptorPool> pool;
        std::unique_ptr<DescriptorSetLayout> inputSetLayout;
        VkDescriptorSet inputSet = VK_NULL_HANDLE;
        VkSampler colorSampler = VK_NULL_HANDLE;
        VkSampler stencilSampler = VK_NULL_HANDLE;
        bool bloomEnabled = false;

        void createSampler();
        void createRenderPass(VkFormat colorFmt);
        void createFramebuffer(const GPUImage &colorImage);
        std::unique_ptr<Pipeline> bloomExtractPipeline;
        std::unique_ptr<Pipeline> bloomBlurHPipeline;
        std::unique_ptr<Pipeline> bloomBlurVPipeline;
        VkPipelineLayout bloomPipelineLayout = VK_NULL_HANDLE;
        std::unique_ptr<DescriptorPool> bloomPool;
        std::unique_ptr<DescriptorSetLayout> bloomSetLayout;
        VkDescriptorSet bloomExtractSet = VK_NULL_HANDLE;
        VkDescriptorSet bloomBlurHSet = VK_NULL_HANDLE;
        VkDescriptorSet bloomBlurVSet = VK_NULL_HANDLE;
        std::unique_ptr<GPUImage> bloomBright;
        std::unique_ptr<GPUImage> bloomBlurH;
        std::unique_ptr<GPUImage> bloomBlurred;
        VkExtent2D bloomExtent = {};
        bool bloomImagesInitialized = false;

        void createBloomImages();
        void resolveGlobalVolume(entt::registry &registry);
        void createDescriptors(const GPUImage &colorImage, VkImageLayout colorLayout, const GPUImage &depthImage);
        void createBloomDescriptors(const GPUImage &colorImage, VkImageLayout colorLayout);
        void createPipelineLayouts();
        void createPipeline();
        void createBloomPipelines();
        void recordBypass(VkCommandBuffer cmd);
        void recordBloom(VkCommandBuffer cmd, VkDescriptorSet globalSet);
        void transitionUndefinedToGeneral(VkCommandBuffer cmd, VkImage image) const;
        void ensureBloomImagesInitialized(VkCommandBuffer cmd);
        void barrierGeneralToGeneral(VkCommandBuffer cmd, VkImage image) const;
        void barrierGeneralToFragmentRead(VkCommandBuffer cmd, VkImage image) const;

        bool activeBloomEnabled = false;
        bool activeVignetteEnabled = false;
        TonemappingMode activeTonemapping = TonemappingMode::NONE;
        float activeExposure = 1.0f;
        float activeContrast = 1.0f;
        float activeSaturation = 1.0f;
        glm::vec4 activeColorTint = glm::vec4(1.0f);
        float activeBloomStrength = 3.0f;
        float activeVignetteStrength = 2.0f;
    };
} // namespace Atlas
