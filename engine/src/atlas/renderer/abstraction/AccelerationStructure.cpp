#include "AccelerationStructure.hpp"
#include "core/Log.hpp"

namespace Atlas {
    VkAccelerationStructureKHR AccelerationStructure::createHandle(Device &device, VkAccelerationStructureTypeKHR type, VkDeviceSize size, std::unique_ptr<GPUBuffer> &outBuffer) {
        outBuffer = std::make_unique<GPUBuffer>(
            GPUBuffer::simple(device)
            .setSize(size)
            .setUsage(VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
            .setMemoryUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
            .build()
        );

        VkAccelerationStructureCreateInfoKHR asCI{};
        asCI.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        asCI.buffer = outBuffer->get();
        asCI.size = size;
        asCI.type = type;

        VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
        if (vkCreateAccelerationStructureKHR(device.device(), &asCI, nullptr, &handle) != VK_SUCCESS)
            throw std::runtime_error("AccelerationStructure: vkCreateAccelerationStructureKHR failed");

        return handle;
    }

    void AccelerationStructure::build(Device &device, const VkAccelerationStructureBuildGeometryInfoKHR &buildInfo, const VkAccelerationStructureBuildRangeInfoKHR &rangeInfo, VkDeviceSize scratchSize) {
        auto scratch = GPUBuffer::simple(device)
                .setSize(scratchSize)
                .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
                .setMemoryUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
                .build();

        VkBufferDeviceAddressInfo addrInfo{};
        addrInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addrInfo.buffer = scratch.get();
        const VkDeviceAddress scratchAddress = vkGetBufferDeviceAddress(device.device(), &addrInfo);

        VkAccelerationStructureBuildGeometryInfoKHR patchedBuildInfo = buildInfo;
        patchedBuildInfo.scratchData.deviceAddress = scratchAddress;

        const VkAccelerationStructureBuildRangeInfoKHR *pRangeInfo = &rangeInfo;

        VkCommandBuffer cmd = device.beginSingleTimeCommands();

        vkCmdBuildAccelerationStructuresKHR(cmd, 1, &patchedBuildInfo, &pRangeInfo);
        device.endSingleTimeCommands(cmd);
    }

    AccelerationStructure AccelerationStructure::buildBLAS(Device &device, VkDeviceAddress vertexBufferAddress, VkDeviceAddress indexBufferAddress, uint32_t vertexCount, uint32_t indexCount, VkDeviceSize vertexStride) {
        VkAccelerationStructureGeometryTrianglesDataKHR triangles{};
        triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        triangles.vertexData.deviceAddress = vertexBufferAddress;
        triangles.vertexStride = vertexStride;
        triangles.maxVertex = vertexCount - 1;
        triangles.indexType = VK_INDEX_TYPE_UINT32;
        triangles.indexData.deviceAddress = indexBufferAddress;
        triangles.transformData = {};

        VkAccelerationStructureGeometryKHR geometry{};
        geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geometry.geometry.triangles = triangles;
        geometry.flags = 0;

        VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
        buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.geometryCount = 1;
        buildInfo.pGeometries = &geometry;

        const uint32_t primitiveCount = indexCount / 3;

        VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
        sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
        vkGetAccelerationStructureBuildSizesKHR(device.device(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &primitiveCount, &sizeInfo);

        AccelerationStructure as;
        as.device_ = &device;
        as.handle_ = createHandle(device, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, sizeInfo.accelerationStructureSize, as.buffer_);

        VkAccelerationStructureDeviceAddressInfoKHR addrInfo{};
        addrInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
        addrInfo.accelerationStructure = as.handle_;
        as.deviceAddress_ = vkGetAccelerationStructureDeviceAddressKHR(device.device(), &addrInfo);

        buildInfo.dstAccelerationStructure = as.handle_;

        VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
        rangeInfo.primitiveCount = primitiveCount;
        rangeInfo.primitiveOffset = 0;
        rangeInfo.firstVertex = 0;
        rangeInfo.transformOffset = 0;

        build(device, buildInfo, rangeInfo, sizeInfo.buildScratchSize);

        AT_INFO("BLAS built: {} primitives, {} bytes", primitiveCount, sizeInfo.accelerationStructureSize);
        return as;
    }

    AccelerationStructure AccelerationStructure::buildTLAS(Device &device, const std::vector<VkAccelerationStructureInstanceKHR> &instances) {
        const auto instanceCount = static_cast<uint32_t>(instances.size());

        // Staging buffer — host-visible so getMapped() is guaranteed non-null
        GPUBuffer instanceStaging(
            device,
            sizeof(VkAccelerationStructureInstanceKHR) * instanceCount,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
        );
        instanceStaging.map();
        memcpy(instanceStaging.getMapped(),
               instances.data(),
               sizeof(VkAccelerationStructureInstanceKHR) * instanceCount);
        instanceStaging.flush(sizeof(VkAccelerationStructureInstanceKHR) * instanceCount);
        instanceStaging.unmap();

        // Device-local instance buffer readable by the AS build
        auto instanceBuffer = GPUBuffer::simple(device)
                .setSize(sizeof(VkAccelerationStructureInstanceKHR) * instanceCount)
                .setUsage(VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
                        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
                        | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
                .setMemoryUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
                .build();

        GPUBuffer::copy(device,
                        instanceStaging.get(), instanceBuffer.get(),
                        sizeof(VkAccelerationStructureInstanceKHR) * instanceCount,
                        0, 0);

        VkBufferDeviceAddressInfo instAddrInfo{};
        instAddrInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        instAddrInfo.buffer = instanceBuffer.get();
        const VkDeviceAddress instanceBufferAddress = vkGetBufferDeviceAddress(device.device(), &instAddrInfo);

        VkAccelerationStructureGeometryInstancesDataKHR instancesData{};
        instancesData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        instancesData.arrayOfPointers = VK_FALSE;
        instancesData.data.deviceAddress = instanceBufferAddress;

        VkAccelerationStructureGeometryKHR geometry{};
        geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        geometry.geometry.instances = instancesData;

        VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
        buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.geometryCount = 1;
        buildInfo.pGeometries = &geometry;

        VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
        sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
        vkGetAccelerationStructureBuildSizesKHR(
            device.device(),
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &buildInfo,
            &instanceCount,
            &sizeInfo);

        AccelerationStructure as;
        as.device_ = &device;
        as.handle_ = createHandle(
            device,
            VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
            sizeInfo.accelerationStructureSize,
            as.buffer_);

        VkAccelerationStructureDeviceAddressInfoKHR addrInfo{};
        addrInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
        addrInfo.accelerationStructure = as.handle_;
        as.deviceAddress_ = vkGetAccelerationStructureDeviceAddressKHR(
            device.device(), &addrInfo);

        buildInfo.dstAccelerationStructure = as.handle_;

        VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
        rangeInfo.primitiveCount = instanceCount;
        rangeInfo.primitiveOffset = 0;
        rangeInfo.firstVertex = 0;
        rangeInfo.transformOffset = 0;

        build(device, buildInfo, rangeInfo, sizeInfo.buildScratchSize);

        // instanceBuffer destroyed here automatically
        AT_INFO("TLAS built: {} instances, {} bytes", instanceCount, sizeInfo.accelerationStructureSize);
        return as;
    }

    AccelerationStructure::~AccelerationStructure() {
        destroy();
    }

    AccelerationStructure::AccelerationStructure(AccelerationStructure &&other) noexcept
        : device_(other.device_)
          , handle_(other.handle_)
          , buffer_(std::move(other.buffer_))
          , deviceAddress_(other.deviceAddress_) {
        other.device_ = nullptr;
        other.handle_ = VK_NULL_HANDLE;
        other.deviceAddress_ = 0;
    }

    AccelerationStructure &AccelerationStructure::operator=(AccelerationStructure &&other) noexcept {
        if (this != &other) {
            destroy();
            device_ = other.device_;
            handle_ = other.handle_;
            buffer_ = std::move(other.buffer_);
            deviceAddress_ = other.deviceAddress_;
            other.device_ = nullptr;
            other.handle_ = VK_NULL_HANDLE;
            other.deviceAddress_ = 0;
        }
        return *this;
    }

    void AccelerationStructure::destroy() {
        if (handle_ == VK_NULL_HANDLE) {
            return;
        }

        vkDestroyAccelerationStructureKHR(device_->device(), handle_, nullptr);
        buffer_.reset();
        handle_ = VK_NULL_HANDLE;
        deviceAddress_ = 0;
    }
} // namespace Atlas
