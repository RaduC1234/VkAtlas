#pragma once

#include <entt/entity/registry.hpp>

#include "CullingStage.hpp"
#include "IRenderStage.hpp"
#include "entity/Object.hpp"
#include "renderer/Device.hpp"
#include "renderer/abstraction/Descriptors.hpp"
#include "renderer/abstraction/GPUImage.hpp"
#include "renderer/abstraction/Pipeline.hpp"

namespace Atlas {
    class GeometryStage : public IRenderStage {
    public:
        GeometryStage(Device &device, AssetManager &assets,
                      const DescriptorSetLayout &globalSetLayout,
                      CullingStage &culling);
        ~GeometryStage() override;

        GeometryStage(const GeometryStage &) = delete;
        GeometryStage &operator=(const GeometryStage &) = delete;

        VkRenderPass     getRenderPass()    const { return renderPass; }
        const GPUImage  &getColorTarget()   const { return *colorTarget; }
        const GPUImage  &getDepthTarget()   const { return *depthTarget; }

        void getDeclaredOutputs(std::vector<Resource::Description> &out) const override;
        void getDeclaredInputs(std::vector<std::string> &out)            const override;

        void onResourcesCreated(const Context &ctx) override;
        void onUpdate(entt::registry &registry)     override;

        void record(VkCommandBuffer cmd, VkDescriptorSet globalSet) override;

    private:
        Device                      &device;
        AssetManager                &assets;
        const DescriptorSetLayout   &globalSetLayout;
        CullingStage                &cullingStage_;

        const GPUImage *colorTarget = nullptr;
        const GPUImage *depthTarget = nullptr;

        VkRenderPass    renderPass   = VK_NULL_HANDLE;
        VkFramebuffer   framebuffer  = VK_NULL_HANDLE;
        VkExtent2D      extent       = {};

        std::unique_ptr<Pipeline> opaquePipeline;
        std::unique_ptr<Pipeline> skyboxPipeline;
        VkPipelineLayout          pipelineLayout = VK_NULL_HANDLE;

        std::unique_ptr<DescriptorPool> pool;

        std::unique_ptr<DescriptorSetLayout> environmentSetLayout;
        VkDescriptorSet environmentSet = VK_NULL_HANDLE;

        std::unique_ptr<DescriptorSetLayout> objectDataSetLayout;
        VkDescriptorSet objectDataSet = VK_NULL_HANDLE;

        std::unique_ptr<DescriptorSetLayout> lightSetLayout;
        VkDescriptorSet lightSet = VK_NULL_HANDLE;

        std::unique_ptr<DescriptorSetLayout> skyboxSetLayout;
        VkDescriptorSet skyboxDescriptorSet = VK_NULL_HANDLE;
        AssetHandle<Cubemap> boundIrradianceHandle;
        bool boundIrradianceReady = false;
        AssetHandle<Cubemap> boundPrefilterHandle;
        bool boundPrefilterReady = false;
        AssetHandle<Cubemap> boundSkyboxHandle;
        bool boundSkyboxReady = false;

        void begin(VkCommandBuffer cmd);
        void end(VkCommandBuffer cmd);

        void createRenderPass();
        void createFramebuffer();
        void createPipelineLayout();
        void createPipelines();
        void createDescriptors();

        void updateSkyboxDescriptors(const SkyboxComponent &skybox);

        static VkPipelineDepthStencilStateCreateInfo makeStencilWrite(uint8_t ref);
    };
} // namespace Atlas
