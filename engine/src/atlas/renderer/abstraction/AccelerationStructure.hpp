#pragma once

#include <memory>
#include <vector>
#include <vulkan/vulkan.h>

#include "renderer/Device.hpp"
#include "renderer/abstraction/GPUBuffer.hpp"

namespace Atlas {
    class AccelerationStructure {
    public:
        AccelerationStructure() = default;
        ~AccelerationStructure();

        AccelerationStructure(const AccelerationStructure &) = delete;
        AccelerationStructure &operator=(const AccelerationStructure &) = delete;
        AccelerationStructure(AccelerationStructure &&other) noexcept;
        AccelerationStructure &operator=(AccelerationStructure &&other) noexcept;

        // -------------------------------------------------------------------------
        // BLAS — async two-phase path
        // -------------------------------------------------------------------------

        // Phase 1 — allocates AS handle, backing buffer, scratch buffer.
        // Pure device queries + vmaCreateBuffer, no command recording.
        // Called from GPUMesh constructor.
        static AccelerationStructure allocateBLAS(Device &device, VkDeviceAddress vertexBufferAddress, VkDeviceAddress indexBufferAddress, uint32_t vertexCount, uint32_t indexCount, VkDeviceSize vertexStride);

        // Phase 2 — records vkCmdBuildAccelerationStructuresKHR into shared cmd buffer.
        // Called from GPUMesh::recordTransfer() after the copy barrier.
        void recordBuild(VkCommandBuffer cmd);

        // Phase 3 — frees scratch buffer.
        // Called from GPUMesh::onTransferComplete().
        void onBuildComplete();

        static AccelerationStructure buildTLAS(Device &device, const std::vector<VkAccelerationStructureInstanceKHR> &instances);

        VkAccelerationStructureKHR handle() const { return handle_; }
        VkDeviceAddress deviceAddress() const { return deviceAddress_; }
        bool isValid() const { return handle_ != VK_NULL_HANDLE; }

    private:
        static VkAccelerationStructureKHR createHandle(Device &device, VkAccelerationStructureTypeKHR type, VkDeviceSize size, std::unique_ptr<GPUBuffer> &outBuffer);
        static void buildSync(Device &device, const VkAccelerationStructureBuildGeometryInfoKHR &buildInfo, const VkAccelerationStructureBuildRangeInfoKHR &rangeInfo, VkDeviceSize scratchSize);

        void destroy();

        Device *device_ = nullptr;
        VkAccelerationStructureKHR handle_ = VK_NULL_HANDLE;
        std::unique_ptr<GPUBuffer> buffer_;
        VkDeviceAddress deviceAddress_ = 0;

        // BLAS async build state — alive from allocateBLAS() → onBuildComplete()
        std::unique_ptr<GPUBuffer> scratchBuffer_;
        VkAccelerationStructureBuildGeometryInfoKHR buildInfo_{};
        VkAccelerationStructureBuildRangeInfoKHR rangeInfo_{};
        VkAccelerationStructureGeometryKHR geometry_{};
    };
} // namespace Atlas
