#pragma once

#include <entt/entity/registry.hpp>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "RenderStage.hpp"
#include "entity/Object.hpp"
#include "renderer/Device.hpp"
#include "renderer/abstraction/Descriptors.hpp"
#include "renderer/abstraction/GPUImage.hpp"
#include "renderer/abstraction/Pipeline.hpp"

namespace Atlas {
    class GeometryStage : public RenderStage {
    public:
        GeometryStage(Device &device, AssetManager &assets,
                      const DescriptorSetLayout &globalSetLayout);
        ~GeometryStage() override;

        GeometryStage(const GeometryStage &) = delete;
        GeometryStage &operator=(const GeometryStage &) = delete;

        VkRenderPass getRenderPass() const { return renderPass; }
        const GPUImage &getColorTarget() const { return *colorTarget; }
        const GPUImage &getDepthTarget() const { return *depthTarget; }

        void getDeclaredOutputs(std::vector<Resource::Description> &out) const override;
        void getDeclaredInputs(std::vector<std::string> &out) const override;

        void onResourcesCreated(const Context &ctx) override;
        void onUpdate(entt::registry &registry) override;

        void record(VkCommandBuffer cmd, VkDescriptorSet globalSet) override;

    private:
        static constexpr uint32_t MAX_TEXTURES = 1024;

        using RasterDraw = std::pair<AssetHandle<Mesh>, uint32_t>;
        using RasterTextureBinding = std::pair<AssetHandle<Texture>, uint32_t>;
        using RasterDrawData = std::tuple<std::vector<RasterDraw>, std::vector<RasterTextureBinding>, uint32_t>;

        Device &device;
        AssetManager &assets;
        const DescriptorSetLayout &globalSetLayout;

        const GPUImage *colorTarget = nullptr;
        const GPUImage *depthTarget = nullptr;
        const RasterDrawData *drawData = nullptr;

        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkExtent2D extent = {};

        std::unique_ptr<Pipeline> opaquePipeline;
        std::unique_ptr<Pipeline> skyboxPipeline;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

        std::unique_ptr<DescriptorPool> pool;

        std::unique_ptr<DescriptorSetLayout> environmentSetLayout;
        VkDescriptorSet environmentSet = VK_NULL_HANDLE;

        std::unique_ptr<DescriptorPool> texturePool;
        std::unique_ptr<DescriptorSetLayout> textureSetLayout;
        VkDescriptorSet textureSet = VK_NULL_HANDLE;
        uint32_t boundTextureRevision = 0;

        std::unique_ptr<DescriptorSetLayout> objectDataSetLayout;
        VkDescriptorSet objectDataSet = VK_NULL_HANDLE;

        std::unique_ptr<DescriptorSetLayout> lightSetLayout;
        VkDescriptorSet lightSet = VK_NULL_HANDLE;
        VkSampler shadowSampler = VK_NULL_HANDLE;

        std::unique_ptr<DescriptorSetLayout> skyboxSetLayout;
        VkDescriptorSet skyboxDescriptorSet = VK_NULL_HANDLE;
        AssetHandle<Cubemap> boundIrradianceHandle;
        bool boundIrradianceReady = false;
        AssetHandle<Cubemap> boundPrefilterHandle;
        bool boundPrefilterReady = false;
        AssetHandle<Cubemap> boundSkyboxHandle;
        bool boundSkyboxReady = false;
        bool drawSkybox = false;

        void begin(VkCommandBuffer cmd);
        void end(VkCommandBuffer cmd);

        void createRenderPass();
        void createFramebuffer();
        void createPipelineLayout();
        void createPipelines();
        void createDescriptors();

        AssetHandle<Texture> loadRawLookupTexture(const std::string &path, uint32_t width, uint32_t height, VkFormat format);
        void loadLookupTextures();
        void updateTextureDescriptors();
        void updateLookupTextureDescriptors();
        void updateSkyboxDescriptors(const SkyboxComponent &skybox);

        static VkPipelineDepthStencilStateCreateInfo makeStencilWrite(uint8_t ref);

        AssetHandle<Texture> ltcMatLUT;
        AssetHandle<Texture> ltcAmpLUT;
        AssetHandle<Texture> brdfLUT;
        bool boundLtcMatReady = false;
        bool boundLtcAmpReady = false;
        bool boundBrdfReady = false;
    };
} // namespace Atlas
