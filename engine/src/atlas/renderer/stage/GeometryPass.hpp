#pragma once

#include <entt/entity/registry.hpp>

#include "IRenderStage.hpp"
#include "entity/Object.hpp"
#include "renderer/Device.hpp"
#include "renderer/abstraction/Descriptors.hpp"
#include "renderer/abstraction/GPUImage.hpp"
#include "renderer/abstraction/Pipeline.hpp"
#include "utils/Storage.hpp"

namespace Atlas {
    class GeometryPass : public IRenderStage {
    public:
        static constexpr uint32_t MAX_LIGHTS = 32;
        static constexpr uint32_t MAX_TEXTURES = 1024;

        GeometryPass(Device &device, const DescriptorSetLayout &globalSetLayout);
        ~GeometryPass() override;

        GeometryPass(const GeometryPass &) = delete;
        GeometryPass &operator=(const GeometryPass &) = delete;

        VkRenderPass getRenderPass() const { return renderPass; }
        const GPUImage &getColorTarget() const { return *colorTarget; }
        const GPUImage &getDepthTarget() const { return *depthTarget; }

        void getDeclaredOutputs(std::vector<Resource::Description> &out) const override;
        void getDeclaredInputs(std::vector<std::string> &out) const override;

        void onResourcesCreated(const std::unordered_map<std::string, std::reference_wrapper<Resource> > &resources) override;
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

        // render targets (graph-owned)
        const GPUImage *colorTarget = nullptr;
        const GPUImage *depthTarget = nullptr;

        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkExtent2D extent = {};

        std::unique_ptr<Pipeline> opaquePipeline;
        std::unique_ptr<Pipeline> skyboxPipeline;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

        std::unique_ptr<DescriptorPool> pool;

        // environment / IBL
        std::unique_ptr<DescriptorSetLayout> environmentSetLayout;
        VkDescriptorSet environmentSet = VK_NULL_HANDLE;

        // bindless textures — slots filled from texture_handles (CullingPass output)
        std::unique_ptr<DescriptorSetLayout> textureSetLayout;
        VkDescriptorSet bindlessTextureSet = VK_NULL_HANDLE;

        // object data
        std::unique_ptr<DescriptorSetLayout> objectDataSetLayout;
        VkDescriptorSet objectDataSet = VK_NULL_HANDLE;

        // lights — GeometryPass still owns these
        Storage<Light> lights;
        std::unique_ptr<GPUBuffer> lightsBuffer;
        VkDescriptorSet lightSet = VK_NULL_HANDLE;
        std::unique_ptr<DescriptorSetLayout> lightSetLayout;

        // skybox
        std::unique_ptr<DescriptorSetLayout> skyboxSetLayout;
        VkDescriptorSet skyboxDescriptorSet = VK_NULL_HANDLE;
        AssetHandle boundSkyboxHandle = INVALID_ASSET_HANDLE;

        // graph-owned buffers from CullingPass
        const GPUBuffer *sceneVertexBuffer = nullptr;
        const GPUBuffer *sceneIndexBuffer = nullptr;
        const GPUBuffer *opaqueIndirectCmds = nullptr;
        std::vector<AssetHandle> *textureHandles = nullptr; // CPU buffer from CullingPass
        const uint32_t *opaqueDrawCountPtr = nullptr;       // CPU buffer from CullingPass
        // const GPUBuffer           *transparentIndirectCmds = nullptr; // future

        uint32_t opaqueDrawCount = 0;

        void begin(VkCommandBuffer cmd);
        void end(VkCommandBuffer cmd);

        void createRenderPass();
        void createFramebuffer();
        void createPipelineLayout();
        void createPipelines();
        void createDescriptors();
        void createGPUBuffers();

        VkPipelineDepthStencilStateCreateInfo makeStencilWrite(uint8_t ref);
    };
} // namespace Atlas
