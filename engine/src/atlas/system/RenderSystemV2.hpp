#pragma once
#include <entt/entt.hpp>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include "asset/AssetManager.hpp"
#include "entity/Object.hpp"
#include "renderer/Descriptors.hpp"
#include "renderer/Device.hpp"
#include "renderer/Pipeline.hpp"
#include "utils/Storage.hpp"

namespace Atlas {
    class RenderSystemV2 {
    public:
        static constexpr uint32_t MAX_TEXTURES = 1024;
        static constexpr uint32_t MAX_OBJECTS = 10000;
        static constexpr uint32_t MAX_LIGHTS = 5;
        static constexpr VkDeviceSize VERTEX_BUDGET = sizeof(Mesh::Vertex) * 1'000'000;
        static constexpr VkDeviceSize INDEX_BUDGET = sizeof(uint32_t) * 3'000'000;

        RenderSystemV2(Device &device, VkRenderPass renderPass, const DescriptorSetLayout &globalSetLayout);
        ~RenderSystemV2();

        RenderSystemV2(const RenderSystemV2 &) = delete;
        RenderSystemV2 &operator=(const RenderSystemV2 &) = delete;

        // Called once after all assets are loaded. Uploads all geometry, textures,
        // object data, and lights to the GPU — no descriptor updates occur after this.
        void build(entt::registry &registry);

        void cull(VkCommandBuffer computeCommandBuffer, VkDescriptorSet globalSet); // TODO: GPU culling
        void render(VkCommandBuffer graphicsCommandBuffer, VkDescriptorSet globalSet);

    private:
        struct GPUObjectData {
            glm::mat4 modelMatrix;
            glm::mat4 normalMatrix;
            glm::uvec4 textureIndices; // albedo, normal, metallicRoughness, unused
            glm::vec4 baseColor;
        };

        struct MeshAllocation {
            uint32_t firstVertex = 0;
            uint32_t vertexCount = 0;
            uint32_t firstIndex = 0;
            uint32_t indexCount = 0;
        };

        struct Light {
            uint32_t type{static_cast<uint32_t>(LightType::SPOT)};
            float intensity{1.0f};
            float range{0.0f};
            float innerConeAngle{0.0f};
            glm::vec3 color{1.0f};
            float outerConeAngle{glm::radians(45.0f)};
            glm::vec3 position{0.0f};
            float _pad0{0.0f};
            glm::vec3 direction{0.0f, -1.0f, 0.0f};
            float _pad1{0.0f};
        };

        void createDescriptors();
        void createPipelineLayout(const DescriptorSetLayout &globalSetLayout);
        void createPipelines(VkRenderPass renderPass);
        void createGPUBuffers();


        uint32_t registerTexture(AssetHandle handle);
        void registerMesh(AssetHandle handle);
        uint32_t resolveTextureIndex(AssetHandle handle) const;

        Device &device;

        std::unique_ptr<Pipeline> cullingPipeline;
        std::unique_ptr<Pipeline> renderPipeline;
        std::unique_ptr<Pipeline> transparentRenderPipeline;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        std::unique_ptr<DescriptorPool> rendererPool;


        // Set 1 — bindless textures
        std::unique_ptr<DescriptorSetLayout> textureSetLayout;
        VkDescriptorSet bindlessTextureSet = VK_NULL_HANDLE;
        uint32_t nextTextureSlot = 1; // slot 0 = default white
        std::unordered_map<AssetHandle, uint32_t> handleToTextureSlot;
        AssetHandle defaultWhiteTextureHandle = INVALID_ASSET_HANDLE;


        // Set 2 — per-object SSBO
        // Keyed by entt::entity — dense index == firstInstance in the draw command,
        // so the shader can index opaqueObjectData[gl_InstanceIndex] directly.
        std::unique_ptr<DescriptorSetLayout> objectDataSetLayout;

        Storage<GPUObjectData> opaqueObjectData;
        std::unique_ptr<Buffer> objectDataBuffer;
        VkDescriptorSet objectDataSet = VK_NULL_HANDLE;
        std::unique_ptr<Buffer> indirectCommandBuffer;

        Storage<GPUObjectData> transparentObjectData;
        std::unique_ptr<Buffer> transparentObjectDataBuffer;
        VkDescriptorSet transparentObjectDataSet = VK_NULL_HANDLE;
        std::unique_ptr<Buffer> transparentIndirectCommandBuffer;


        // Set 3 — lights SSBO
        Storage<Light> lights;
        std::unique_ptr<Buffer> lightsBuffer;
        VkDescriptorSet lightSet = VK_NULL_HANDLE;
        std::unique_ptr<DescriptorSetLayout> lightSetLayout;


        // Merged geometry buffers
        std::unique_ptr<Buffer> mergedVertexBuffer;
        std::unique_ptr<Buffer> mergedIndexBuffer;
        uint32_t nextVertex = 0;
        uint32_t nextIndex = 0;
        std::unordered_map<AssetHandle, MeshAllocation> meshAllocations;
    };
}
