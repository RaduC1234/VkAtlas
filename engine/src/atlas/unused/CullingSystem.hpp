#pragma once
#include <memory>
#include <entt/entity/registry.hpp>

#include "../renderer/abstraction/Buffer.hpp"
#include "renderer/Camera.hpp"
#include "../renderer/abstraction/Descriptors.hpp"
#include "../renderer/abstraction/Pipeline.hpp"

namespace Atlas {
    struct GPUInstanceData {
        glm::mat4 transform;
        glm::vec4 boundingSphere; // xyz = center, w = radius
        glm::vec3 aabbMin;
        uint32_t meshID;
        glm::vec3 aabbMax;
        uint32_t textureIndex; // Texture index for bindless textures
    };

    struct GPUMeshData {
        uint32_t vertexOffset;
        uint32_t indexOffset;
        uint32_t indexCount;
        uint32_t pad;
    };

    class CullingSystem {
    public:
        CullingSystem(Device &device, const DescriptorSetLayout& globalSetLayout);
        ~CullingSystem();

        CullingSystem(const CullingSystem &) = delete;
        CullingSystem &operator=(const CullingSystem &) = delete;

        void build(entt::registry &registry);
        void setMeshData(const std::vector<GPUMeshData>& meshDataArray);
        void setTextureIndices(const std::vector<uint32_t>& textureIndices);

        void cull(VkCommandBuffer commandBuffer, VkDescriptorSet globalDescriptorSet);

        VkBuffer getDrawCommandBuffer() const { return drawCommandBuffer->get(); }
        VkBuffer getDrawCountBuffer() const { return drawCountBuffer->get(); }
        VkBuffer getInstanceBuffer() const { return instanceBuffer->get(); }

        uint32_t getTotalInstances() const { return totalInstances; }

    private:
        void createBuffers();
        void createDescriptors();
        void createPipelineLayout(const DescriptorSetLayout &globalDescriptorSetLayout);
        void createPipeline();

        Device &device;

        std::unique_ptr<Pipeline> cullingPipeline;
        VkPipelineLayout cullingLayout = VK_NULL_HANDLE;

        // Descriptor management
        std::unique_ptr<DescriptorSetLayout> cullingSetLayout;
        std::unique_ptr<DescriptorPool> cullingPool;
        VkDescriptorSet cullingDescSet = VK_NULL_HANDLE;

        // GPU buffers
        std::unique_ptr<Buffer> instanceBuffer; // Input: all object instances
        std::unique_ptr<Buffer> meshDataBuffer; // Input: mesh metadata
        std::unique_ptr<Buffer> drawCommandBuffer; // Output: visible draw commands (GPU writes)
        std::unique_ptr<Buffer> drawCountBuffer; // Output: number of visible objects (GPU writes)

        // Scene data (CPU-side, built once)
        std::vector<GPUInstanceData> instances;
        std::vector<GPUMeshData> meshes;
        uint32_t totalInstances = 0;
    };
}
