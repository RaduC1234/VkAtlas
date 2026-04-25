#pragma once

#include <entt/entity/registry.hpp>

#include "IRenderStage.hpp"
#include "entity/Object.hpp"
#include "renderer/Device.hpp"
#include "renderer/abstraction/Buffer.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

namespace Atlas {
    class CullingPass final : public IRenderStage {
    public:
        static constexpr uint32_t MAX_OBJECTS = 10000;
        static constexpr VkDeviceSize VERTEX_BUDGET = sizeof(Mesh::Vertex) * 1'000'000;
        static constexpr VkDeviceSize INDEX_BUDGET = sizeof(uint32_t) * 3'000'000;

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

        Buffer *vertexBuffer            = nullptr;
        Buffer *indexBuffer             = nullptr;
        Buffer *opaqueIndirectCmds      = nullptr;
        Buffer *transparentIndirectCmds = nullptr;
        Buffer *objectDataBuffer        = nullptr;

        std::vector<VkDrawIndexedIndirectCommand> opaqueIndirectCommands;
        std::vector<VkDrawIndexedIndirectCommand> transparentIndirectCommands;

        // cluster_buffer — clustered/forward+ light assignment, filled by a future split pass
        // Buffer *clusterBuffer = nullptr;

        void registerMesh(AssetHandle handle);
    };
} // namespace Atlas