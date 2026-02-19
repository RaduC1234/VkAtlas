#pragma once
#include <entt/entt.hpp>

#include "asset/AssetManager.hpp"
#include "renderer/Descriptors.hpp"
#include "renderer/Device.hpp"
#include "renderer/Pipeline.hpp"

namespace Atlas {
    struct GPUObjectData {
        glm::mat4 modelMatrix;
        glm::mat4 normalMatrix;
        glm::uvec4 textureIndices; // albedo, normal, metallicRoughness, unused
        glm::vec4 baseColor; // material base color (RGBA)
    };

    // Tracks where a mesh lives inside the merged vertex/index buffers
    struct MeshAllocation {
        uint32_t firstVertex = 0;
        uint32_t vertexCount = 0;
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
    };

    class RenderSystemV2 {
    public:
        static constexpr uint32_t MAX_TEXTURES = 1024;
        static constexpr uint32_t MAX_OBJECTS = 10000;
        static constexpr VkDeviceSize VERTEX_BUDGET = sizeof(Mesh::Vertex) * 1'000'000; // 1M vertices
        static constexpr VkDeviceSize INDEX_BUDGET = sizeof(uint32_t) * 3'000'000; // 3M indices

        RenderSystemV2(Device &device, VkRenderPass renderPass, const DescriptorSetLayout &globalSetLayout);
        ~RenderSystemV2();

        RenderSystemV2(const RenderSystemV2 &) = delete;
        RenderSystemV2 &operator=(const RenderSystemV2 &) = delete;

        void prepare(entt::registry &registry);
        void rebuildDrawList(entt::registry &registry);

        // Issues one vkCmdDrawIndexedIndirect covering all visible objects
        void render(entt::registry &registry, VkCommandBuffer commandBuffer, VkDescriptorSet globalSet);

    private:
        void createDescriptors();
        void createPipelineLayout(const DescriptorSetLayout &globalSetLayout);
        void createPipeline(VkRenderPass renderPass);
        void createMergedBuffers();
        void createIndirectBuffers();

        // Returns GPU index (slot in descriptor array / mesh allocation map)
        uint32_t registerTexture(AssetHandle handle);
        uint32_t registerMesh(AssetHandle handle); // returns firstIndex into merged buffer

        void commitSamplersToDescriptors();
        void commitMeshesToDescriptors(); // uploads pending mesh data to merged buffers

        Device &device;

        std::unique_ptr<Pipeline> pipeline;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        std::unique_ptr<DescriptorPool> rendererPool;

        // --- Bindless textures (set 1, binding 0) ---
        std::unique_ptr<DescriptorSetLayout> textureSetLayout;
        VkDescriptorSet bindlessTextureSet = VK_NULL_HANDLE;

        uint32_t nextTextureIndex = 1; // 0 = reserved/default
        std::vector<std::shared_ptr<Sampler> > waitingToBeCommitedSamplers;
        std::unordered_map<AssetHandle, uint32_t> handleToGPUIndex;
        AssetHandle defaultWhiteTextureHandle = INVALID_ASSET_HANDLE;

        // --- Object SSBO (set 2, binding 0) ---
        std::unique_ptr<DescriptorSetLayout> objectDataSetLayout;
        VkDescriptorSet objectDataSet = VK_NULL_HANDLE;
        std::unique_ptr<Buffer> objectDataBuffer;

        // --- Merged geometry buffers ---
        std::unique_ptr<Buffer> mergedVertexBuffer;
        std::unique_ptr<Buffer> mergedIndexBuffer;
        uint32_t nextVertex = 0;
        uint32_t nextIndex = 0;

        // Mesh registry: AssetHandle → where it lives in the merged buffers
        std::unordered_map<AssetHandle, MeshAllocation> meshAllocations;

        // Pending mesh uploads: vertices/indices waiting to be copied to GPU
        struct PendingMeshUpload {
            AssetHandle handle;
            std::vector<Mesh::Vertex> vertices;
            std::vector<uint32_t> indices;
            uint32_t firstVertex;
            uint32_t firstIndex;
        };

        std::vector<PendingMeshUpload> pendingMeshUploads;

        // --- Indirect draw ---
        std::unique_ptr<Buffer> indirectCommandBuffer;
        uint32_t currentDrawCount = 0;
    };
}
