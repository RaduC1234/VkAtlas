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
    class GeometryStage : public IRenderStage {
    public:
        static constexpr uint32_t MAX_TEXTURES = 1024;
        static constexpr uint32_t MAX_OBJECTS = 10000;
        static constexpr uint32_t MAX_LIGHTS = 32;
        static constexpr VkDeviceSize VERTEX_BUDGET = sizeof(GPUMesh::Vertex) * 2'000'000;
        static constexpr VkDeviceSize INDEX_BUDGET = sizeof(uint32_t) * 10'000'000;

        GeometryStage(Device &device, AssetManager &assets, const DescriptorSetLayout &globalSetLayout);
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
        struct GPUObjectData {
            glm::mat4 modelMatrix;
            glm::mat4 normalMatrix;
            glm::uvec4 textureIndices;
            glm::vec4 baseColor;
        };

        struct OpaqueDraw {
            AssetHandle<Mesh> mesh;
            uint32_t firstInstance = 0;
        };

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
        AssetManager &assets;
        const DescriptorSetLayout &globalSetLayout;

        const GPUImage *colorTarget = nullptr;
        const GPUImage *depthTarget = nullptr;

        VkRenderPass renderPass = VK_NULL_HANDLE;
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
        std::unordered_map<AssetHandle<Texture>, uint32_t> handleToTextureSlot;

        std::unique_ptr<DescriptorSetLayout> objectDataSetLayout;
        Storage<GPUObjectData> opaqueObjectData;
        std::vector<OpaqueDraw> opaqueDraws;
        std::unique_ptr<GPUBuffer> objectDataBuffer;
        VkDescriptorSet objectDataSet = VK_NULL_HANDLE;
        Storage<Light> lights;
        std::unique_ptr<GPUBuffer> lightsBuffer;
        VkDescriptorSet lightSet = VK_NULL_HANDLE;
        std::unique_ptr<DescriptorSetLayout> lightSetLayout;

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
        void createGPUBuffers();

        uint32_t registerTexture(AssetHandle<Texture> handle);
        uint32_t resolveTextureIndex(AssetHandle<Texture> handle) const;
        void updateSkyboxDescriptors(const SkyboxComponent &skybox);

        static VkPipelineDepthStencilStateCreateInfo makeStencilWrite(uint8_t ref);
    };
} // namespace Atlas
