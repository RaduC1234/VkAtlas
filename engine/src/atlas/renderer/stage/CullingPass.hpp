#pragma once

#include <entt/entity/registry.hpp>

#include "IRenderStage.hpp"
#include "entity/Object.hpp"
#include "renderer/Device.hpp"
#include "renderer/abstraction/GPUBuffer.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

namespace Atlas {
    class CullingPass : public IRenderStage {
    public:
        static constexpr uint32_t MAX_OBJECTS = 10000;
        static constexpr VkDeviceSize VERTEX_BUDGET = sizeof(Mesh::Vertex) * 1'000'000;
        static constexpr VkDeviceSize INDEX_BUDGET = sizeof(uint32_t) * 3'000'000;
        static constexpr uint32_t MAX_TEXTURES = 1024;

        struct GPUObjectData {
            glm::mat4 modelMatrix;
            glm::mat4 normalMatrix;
            glm::uvec4 textureIndices;
            glm::vec4 baseColor;
        };

        CullingPass(Device &device);
        ~CullingPass() override = default;

        CullingPass(const CullingPass &) = delete;
        CullingPass &operator=(const CullingPass &) = delete;

        void getDeclaredOutputs(std::vector<Resource::Description> &out) const override;
        void getDeclaredInputs(std::vector<std::string> &out) const override;

        void onResourcesCreated(const std::unordered_map<std::string, std::reference_wrapper<Resource> > &resources) override;
        void onSceneChanged(entt::registry &registry) override;
        void record(VkCommandBuffer cmd, VkDescriptorSet globalSet) override;

        uint32_t opaqueDrawCount() const { return static_cast<uint32_t>(opaqueIndirectCommands.size()); }

    private:
        struct MeshAllocation {
            uint32_t firstVertex = 0;
            uint32_t vertexCount = 0;
            uint32_t firstIndex  = 0;
            uint32_t indexCount  = 0;
        };

        Device &device;

        uint32_t nextVertex = 0;
        uint32_t nextIndex  = 0;
        std::unordered_map<AssetHandle, MeshAllocation> meshAllocations;

        GPUBuffer *vertexBuffer            = nullptr;
        GPUBuffer *indexBuffer             = nullptr;
        GPUBuffer *opaqueIndirectCmds      = nullptr;
        GPUBuffer *objectDataBuffer        = nullptr;
        std::vector<AssetHandle>* textureHandles = nullptr;

        std::vector<VkDrawIndexedIndirectCommand> opaqueIndirectCommands;
        std::vector<VkDrawIndexedIndirectCommand> transparentIndirectCommands;

        AssetHandle defaultWhiteHandle = INVALID_ASSET_HANDLE;
        std::unordered_map<AssetHandle, uint32_t> textureSlots;

        void registerMesh(AssetHandle handle);
        uint32_t registerTexture(AssetHandle handle);
    };
} // namespace Atlas