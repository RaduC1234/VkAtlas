#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include "IGPUResource.hpp"
#include "asset/Mesh.hpp"
#include "renderer/Device.hpp"
#include "renderer/abstraction/GPUBuffer.hpp"
#include "renderer/abstraction/AccelerationStructure.hpp"

namespace Atlas {
    class GPUMesh final : public IGPUResource {
    public:
        struct Vertex {
            glm::vec3 position{};
            glm::vec3 color{};
            glm::vec3 normal{};
            glm::vec2 uv{};
            glm::vec4 tangent{};

            bool operator==(const Vertex &other) const {
                return position == other.position
                       && color == other.color
                       && normal == other.normal
                       && uv == other.uv;
            }

            static std::vector<VkVertexInputBindingDescription> getBindingDescriptions();
            static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions();
        };

        // Phase 1 — pure CPU, no command recording
        // Allocates vertex/index buffers, fills staging buffers,
        // allocates BLAS handle + scratch buffer.
        GPUMesh(Device &device, const Mesh &mesh);
        ~GPUMesh() override;

        GPUMesh(const GPUMesh &) = delete;
        GPUMesh &operator=(const GPUMesh &) = delete;

        // Phase 2 — records copy + BLAS build into shared transfer cmd buffer
        void recordTransfer(VkCommandBuffer cmd) override;

        // Phase 3 — transfer completion callback, frees staging + scratch buffers
        void onTransferComplete() override;

        // Phase 4 — records ownership acquire barrier for vertex + index buffers
        void recordOwnershipAcquire(VkCommandBuffer cmd) override;

        // Phase 5 — no-op, meshes are not in the bindless texture array
        void updateBindlessSlot() override {
        }

        // Raster path — bind vertex/index buffers directly
        void bind(VkCommandBuffer cmd) const;
        void draw(VkCommandBuffer cmd) const;

        VkBuffer getVertexBuffer() const { return vertexBuffer_->get(); }
        VkBuffer getIndexBuffer() const { return indexBuffer_->get(); }
        uint32_t vertexCount() const { return vertexCount_; }
        uint32_t indexCount() const { return indexCount_; }
        VkDeviceAddress vertexBufferAddress() const;
        VkDeviceAddress indexBufferAddress() const;

        const AccelerationStructure &accelerationStructure() const { return blas_; }

        static std::unique_ptr<GPUMesh> createDefault(Device &device);

    private:
        explicit GPUMesh(Device &device);

        void recordOwnershipRelease(VkCommandBuffer cmd);

        Device &device_;
        uint32_t vertexCount_ = 0;
        uint32_t indexCount_ = 0;

        std::unique_ptr<GPUBuffer> vertexBuffer_;
        std::unique_ptr<GPUBuffer> indexBuffer_;

        std::unique_ptr<GPUBuffer> vertexStaging_;
        std::unique_ptr<GPUBuffer> indexStaging_;

        AccelerationStructure blas_;
    };
} // namespace Atlas
