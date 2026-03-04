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
        static constexpr uint32_t MAX_LIGHTS = 100;
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
            alignas(4) uint32_t type{static_cast<uint32_t>(LightType::SPOT)};
            alignas(4) float intensity{1.0f};
            alignas(4) float range{0.0f};
            alignas(4) float innerConeAngle{0.0f};
            alignas(16) glm::vec3 color{1.0f};
            alignas(4) float outerConeAngle{glm::radians(45.0f)};
            alignas(16) glm::vec3 position{0.0f};
            alignas(4) float _pad{0.0f};
        };

        void createDescriptors();
        void createPipelineLayout(const DescriptorSetLayout &globalSetLayout);
        void createPipeline(VkRenderPass renderPass);
        void createGPUBuffers();

        // Both called during build() only. Descriptors are written immediately —
        // no pending/commit pattern needed since build() runs once outside any command buffer.
        uint32_t registerTexture(AssetHandle handle);
        void registerMesh(AssetHandle handle);

        uint32_t resolveTextureIndex(AssetHandle handle) const;

        Device &device;

        std::unique_ptr<Pipeline> renderPipeline;
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
        // so the shader can index objectData[gl_InstanceIndex] directly.
        std::unique_ptr<DescriptorSetLayout> objectDataSetLayout;
        VkDescriptorSet objectDataSet = VK_NULL_HANDLE;
        std::unique_ptr<Buffer> objectDataBuffer;
        Storage<GPUObjectData> objectData;

        // Set 3 — lights SSBO
        std::unique_ptr<DescriptorSetLayout> lightSetLayout;
        VkDescriptorSet lightSet = VK_NULL_HANDLE;
        std::unique_ptr<Buffer> lightsBuffer;
        Storage<Light> lights;

        // Merged geometry buffers
        std::unique_ptr<Buffer> mergedVertexBuffer;
        std::unique_ptr<Buffer> mergedIndexBuffer;
        uint32_t nextVertex = 0;
        uint32_t nextIndex = 0;
        std::unordered_map<AssetHandle, MeshAllocation> meshAllocations;

        // Indirect draw commands — firstInstance per draw == dense index in objectData
        std::unique_ptr<Buffer> indirectCommandBuffer;
    };
}
