#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>

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

        static AccelerationStructure buildBLAS(Device &device, VkDeviceAddress vertexBufferAddress, VkDeviceAddress indexBufferAddress, uint32_t vertexCount, uint32_t indexCount, VkDeviceSize vertexStride);
        static AccelerationStructure buildTLAS(Device &device, const std::vector<VkAccelerationStructureInstanceKHR> &instances);

        VkAccelerationStructureKHR handle() const { return handle_; }
        VkDeviceAddress deviceAddress() const { return deviceAddress_; }
        bool isValid() const { return handle_ != VK_NULL_HANDLE; }

    private:
        static VkAccelerationStructureKHR createHandle(
            Device &device,
            VkAccelerationStructureTypeKHR type,
            VkDeviceSize size,
            std::unique_ptr<GPUBuffer> &outBuffer
        );

        static void build(
            Device &device,
            const VkAccelerationStructureBuildGeometryInfoKHR &buildInfo,
            const VkAccelerationStructureBuildRangeInfoKHR &rangeInfo,
            VkDeviceSize scratchSize
        );

        void destroy();

        Device *device_ = nullptr;
        VkAccelerationStructureKHR handle_ = VK_NULL_HANDLE;
        std::unique_ptr<GPUBuffer> buffer_;
        VkDeviceAddress deviceAddress_ = 0;
    };
} // namespace Atlas
