#pragma once

#include <entt/entity/registry.hpp>

#include "IRenderStage.hpp"
#include "CullingPass.hpp"
#include "entity/Object.hpp"
#include "renderer/Device.hpp"
#include "renderer/abstraction/Descriptors.hpp"
#include "renderer/abstraction/GPUImage.hpp"
#include "renderer/abstraction/Pipeline.hpp"
#include "utils/Storage.hpp"

namespace Atlas {
    class GeometryPass : public IRenderStage {
    public:
        static constexpr uint32_t MAX_TEXTURES = 1024;
        static constexpr uint32_t MAX_LIGHTS   = 32;

        GeometryPass(Device &device, const DescriptorSetLayout &globalSetLayout);
        ~GeometryPass() override;

        GeometryPass(const GeometryPass &) = delete;
        GeometryPass &operator=(const GeometryPass &) = delete;

        VkRenderPass getRenderPass() const { return renderPass; }
        const GPUImage &getColorTarget() const { return *colorTarget; }
        const GPUImage &getDepthTarget() const { return *depthTarget; }

        void getDeclaredOutputs(std::vector<Resource::Description> &out) const override;
        void getDeclaredInputs(std::vector<std::string> &out) const override;

        void onResourcesCreated(const std::unordered_map<std::string, std::reference_wrapper<Resource>> &resources) override;
        void onSceneChanged(entt::registry &registry) override;

        void record(VkCommandBuffer cmd, VkDescriptorSet globalSet) override;

    private:
        struct Light {
            uint32_t type{static_cast<uint32_t>(LightType::SPOT)};
            float intensity{1.0f};
            float range{0.0f};
            float innerConeAngle{0.0f};
            glm::vec3 color{1.0f};
            float outerConeAngle{glm::radians(45.0f)};
            glm::vec3 position{0.0f};
            float width{0.0f};
            glm::vec3 direction{0.0f, -1.0f, 0.0f};
            float height{0.0f};
        };

        Device &device;
        const DescriptorSetLayout &globalSetLayout;

        const GPUImage *colorTarget = nullptr;
        const GPUImage *depthTarget = nullptr;

        VkRenderPass renderPass   = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkExtent2D extent = {};

        std::unique_ptr<Pipeline> opaquePipeline;
        std::unique_ptr<Pipeline> skyboxPipeline;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

        std::unique_ptr<DescriptorPool> pool;

        std::unique_ptr<DescriptorSetLayout> environmentSetLayout;
        VkDescriptorSet environmentSet = VK_NULL_HANDLE;

        std::unique_ptr<DescriptorSetLayout> textureSetLayout;
        VkDescriptorSet bindlessTextureSet = VK_NULL_HANDLE;
        uint32_t nextTextureSlot = 1;
        std::unordered_map<AssetHandle, uint32_t> handleToTextureSlot;
        AssetHandle defaultWhiteHandle = INVALID_ASSET_HANDLE;

        std::unique_ptr<DescriptorSetLayout> objectDataSetLayout;
        VkDescriptorSet objectDataSet = VK_NULL_HANDLE;

        Storage<Light> lights;
        std::unique_ptr<Buffer> lightsBuffer;
        VkDescriptorSet lightSet = VK_NULL_HANDLE;
        std::unique_ptr<DescriptorSetLayout> lightSetLayout;

        std::unique_ptr<DescriptorSetLayout> skyboxSetLayout;
        VkDescriptorSet skyboxDescriptorSet = VK_NULL_HANDLE;
        AssetHandle boundSkyboxHandle = INVALID_ASSET_HANDLE;

        // graph-owned, consumed from CullingPass outputs
        const Buffer *sceneVertexBuffer  = nullptr;
        const Buffer *sceneIndexBuffer   = nullptr;
        const Buffer *opaqueIndirectCmds = nullptr;
        // transparent_indirect_cmds — reserved for a future transparent pass
        // const Buffer *transparentIndirectCmds = nullptr;

        uint32_t opaqueDrawCount = 0;

        void begin(VkCommandBuffer cmd);
        void end(VkCommandBuffer cmd);
        void barrier(VkCommandBuffer cmd);

        void createRenderPass();
        void createFramebuffer();
        void createPipelineLayout();
        void createPipelines();
        void createDescriptors();
        void createGPUBuffers();

        uint32_t registerTexture(AssetHandle handle);
        uint32_t resolveTextureIndex(AssetHandle handle) const;
        VkPipelineDepthStencilStateCreateInfo makeStencilWrite(uint8_t ref);
    };
} // namespace Atlas