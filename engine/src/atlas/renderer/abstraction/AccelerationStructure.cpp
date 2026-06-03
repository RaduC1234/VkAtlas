#include "AccelerationStructure.hpp"

#include "core/Log.hpp"

namespace Atlas {
    namespace {
        VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment) {
            if (alignment == 0) {
                return value;
            }
            return ((value + alignment - 1) / alignment) * alignment;
        }

        VkDeviceSize scratchAllocationSize(Device &device, VkDeviceSize scratchSize) {
            const VkDeviceSize alignment = device.accelerationStructureProperties().minAccelerationStructureScratchOffsetAlignment;
            return scratchSize + (alignment > 0 ? alignment - 1 : 0);
        }

        VkDeviceAddress alignedScratchAddress(Device &device, VkBuffer scratchBuffer) {
            VkBufferDeviceAddressInfo addrInfo{};
            addrInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
            addrInfo.buffer = scratchBuffer;

            const VkDeviceAddress address = vkGetBufferDeviceAddress(device.device(), &addrInfo);
            const VkDeviceSize alignment = device.accelerationStructureProperties().minAccelerationStructureScratchOffsetAlignment;
            return alignUp(address, alignment);
        }
    }

    // -------------------------------------------------------------------------
    // allocateBLAS — Phase 1, pure CPU + device queries, no cmd recording
    // -------------------------------------------------------------------------

    AccelerationStructure AccelerationStructure::allocateBLAS(Device &device, VkDeviceAddress vertexBufferAddress, VkDeviceAddress indexBufferAddress, uint32_t vertexCount, uint32_t indexCount, VkDeviceSize vertexStride) {
        AccelerationStructure as;
        as.device_ = &device;

        // Store geometry — pGeometries pointer must remain valid until recordBuild()
        as.geometry_.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        as.geometry_.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        as.geometry_.flags = 0;

        auto &tri = as.geometry_.geometry.triangles;
        tri.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        tri.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        tri.vertexData.deviceAddress = vertexBufferAddress;
        tri.vertexStride = vertexStride;
        tri.maxVertex = vertexCount - 1;
        tri.indexType = VK_INDEX_TYPE_UINT32;
        tri.indexData.deviceAddress = indexBufferAddress;
        tri.transformData = {};

        const uint32_t primitiveCount = indexCount / 3;

        as.buildInfo_.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        as.buildInfo_.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        as.buildInfo_.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        as.buildInfo_.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        as.buildInfo_.geometryCount = 1;
        as.buildInfo_.pGeometries = &as.geometry_;

        as.rangeInfo_.primitiveCount = primitiveCount;
        as.rangeInfo_.primitiveOffset = 0;
        as.rangeInfo_.firstVertex = 0;
        as.rangeInfo_.transformOffset = 0;

        // Query AS and scratch sizes — device query, no cmds
        VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
        sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
        vkGetAccelerationStructureBuildSizesKHR(
            device.device(),
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &as.buildInfo_, &primitiveCount, &sizeInfo);

        // Allocate AS backing buffer + handle — no cmds
        as.handle_ = createHandle(
            device,
            VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
            sizeInfo.accelerationStructureSize,
            as.buffer_);

        VkAccelerationStructureDeviceAddressInfoKHR addrInfo{};
        addrInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
        addrInfo.accelerationStructure = as.handle_;
        as.deviceAddress_ = vkGetAccelerationStructureDeviceAddressKHR(device.device(), &addrInfo);

        as.buildInfo_.dstAccelerationStructure = as.handle_;

        // Allocate scratch buffer — no cmds
        as.scratchBuffer_ = std::make_unique<GPUBuffer>(
            GPUBuffer::simple(device)
            .setSize(scratchAllocationSize(device, sizeInfo.buildScratchSize))
            .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
            .setMemoryUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
            .build());

        as.buildInfo_.scratchData.deviceAddress = alignedScratchAddress(device, as.scratchBuffer_->get());

        AT_INFO("BLAS allocated: {} primitives, {} bytes",
                primitiveCount, sizeInfo.accelerationStructureSize);
        return as;
    }

    // -------------------------------------------------------------------------
    // recordBuild — Phase 2, records into shared cmd buffer
    // -------------------------------------------------------------------------

    void AccelerationStructure::recordBuild(VkCommandBuffer cmd) {
        const VkAccelerationStructureBuildRangeInfoKHR *pRange = &rangeInfo_;
        vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo_, &pRange);
    }

    // -------------------------------------------------------------------------
    // onBuildComplete — Phase 3, free scratch buffer
    // -------------------------------------------------------------------------

    void AccelerationStructure::onBuildComplete() {
        scratchBuffer_.reset();
        buildInfo_ = {};
        geometry_ = {};
        rangeInfo_ = {};
    }

    // -------------------------------------------------------------------------
    // buildTLAS — synchronous, rebuilt every frame
    // -------------------------------------------------------------------------

    AccelerationStructure AccelerationStructure::buildTLAS(
        Device &device,
        const std::vector<VkAccelerationStructureInstanceKHR> &instances) {
        const auto instanceCount = static_cast<uint32_t>(instances.size());

        // Staging buffer for instance data
        GPUBuffer instanceStaging(
            device,
            sizeof(VkAccelerationStructureInstanceKHR) * instanceCount,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
            VMA_ALLOCATION_CREATE_MAPPED_BIT);

        instanceStaging.map();
        memcpy(instanceStaging.getMapped(),
               instances.data(),
               sizeof(VkAccelerationStructureInstanceKHR) * instanceCount);
        instanceStaging.flush(sizeof(VkAccelerationStructureInstanceKHR) * instanceCount);
        instanceStaging.unmap();

        auto instanceBuffer = GPUBuffer::simple(device)
                .setSize(sizeof(VkAccelerationStructureInstanceKHR) * instanceCount)
                .setUsage(VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
                          | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
                          | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
                .setMemoryUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
                .build();

        GPUBuffer::copy(device,
                        instanceStaging.get(), instanceBuffer.get(),
                        sizeof(VkAccelerationStructureInstanceKHR) * instanceCount);

        VkBufferDeviceAddressInfo instAddrInfo{};
        instAddrInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        instAddrInfo.buffer = instanceBuffer.get();
        const VkDeviceAddress instanceBufferAddress =
                vkGetBufferDeviceAddress(device.device(), &instAddrInfo);

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
            &buildInfo, &instanceCount, &sizeInfo);

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
        as.deviceAddress_ = vkGetAccelerationStructureDeviceAddressKHR(device.device(), &addrInfo);

        buildInfo.dstAccelerationStructure = as.handle_;

        VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
        rangeInfo.primitiveCount = instanceCount;
        rangeInfo.primitiveOffset = 0;
        rangeInfo.firstVertex = 0;
        rangeInfo.transformOffset = 0;

        buildSync(device, buildInfo, rangeInfo, sizeInfo.buildScratchSize);

        AT_INFO("TLAS built: {} instances, {} bytes", instanceCount, sizeInfo.accelerationStructureSize);
        return as;
    }

    // -------------------------------------------------------------------------
    // buildSync — synchronous build, used by TLAS only
    // -------------------------------------------------------------------------

    void AccelerationStructure::buildSync(
        Device &device,
        const VkAccelerationStructureBuildGeometryInfoKHR &buildInfo,
        const VkAccelerationStructureBuildRangeInfoKHR &rangeInfo,
        VkDeviceSize scratchSize) {
        auto scratch = GPUBuffer::simple(device)
                .setSize(scratchAllocationSize(device, scratchSize))
                .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
                .setMemoryUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
                .build();

        VkAccelerationStructureBuildGeometryInfoKHR patchedBuildInfo = buildInfo;
        patchedBuildInfo.scratchData.deviceAddress = alignedScratchAddress(device, scratch.get());

        const VkAccelerationStructureBuildRangeInfoKHR *pRange = &rangeInfo;

        VkCommandBuffer cmd = device.beginGraphicsCommands();
        vkCmdBuildAccelerationStructuresKHR(cmd, 1, &patchedBuildInfo, &pRange);
        device.endGraphicsCommands(cmd);
    }

    // -------------------------------------------------------------------------
    // createHandle — allocates AS backing buffer + vkCreateAccelerationStructureKHR
    // -------------------------------------------------------------------------

    VkAccelerationStructureKHR AccelerationStructure::createHandle(
        Device &device,
        VkAccelerationStructureTypeKHR type,
        VkDeviceSize size,
        std::unique_ptr<GPUBuffer> &outBuffer) {
        outBuffer = std::make_unique<GPUBuffer>(
            GPUBuffer::simple(device)
            .setSize(size)
            .setUsage(VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
            .setMemoryUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
            .build());

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

    // -------------------------------------------------------------------------
    // Lifetime management
    // -------------------------------------------------------------------------

    AccelerationStructure::~AccelerationStructure() {
        destroy();
    }

    AccelerationStructure::AccelerationStructure(AccelerationStructure &&other) noexcept
        : device_(other.device_)
          , handle_(other.handle_)
          , buffer_(std::move(other.buffer_))
          , deviceAddress_(other.deviceAddress_)
          , scratchBuffer_(std::move(other.scratchBuffer_))
          , buildInfo_(other.buildInfo_)
          , rangeInfo_(other.rangeInfo_)
          , geometry_(other.geometry_) {
        other.device_ = nullptr;
        other.handle_ = VK_NULL_HANDLE;
        other.deviceAddress_ = 0;
        other.buildInfo_ = {};
        other.rangeInfo_ = {};
        other.geometry_ = {};
        // Fix up pGeometries pointer — it was pointing into other.geometry_
        if (handle_ != VK_NULL_HANDLE)
            buildInfo_.pGeometries = &geometry_;
    }

    AccelerationStructure &AccelerationStructure::operator=(AccelerationStructure &&other) noexcept {
        if (this != &other) {
            destroy();
            device_ = other.device_;
            handle_ = other.handle_;
            buffer_ = std::move(other.buffer_);
            deviceAddress_ = other.deviceAddress_;
            scratchBuffer_ = std::move(other.scratchBuffer_);
            buildInfo_ = other.buildInfo_;
            rangeInfo_ = other.rangeInfo_;
            geometry_ = other.geometry_;

            other.device_ = nullptr;
            other.handle_ = VK_NULL_HANDLE;
            other.deviceAddress_ = 0;
            other.buildInfo_ = {};
            other.rangeInfo_ = {};
            other.geometry_ = {};

            // Fix up pGeometries pointer — it was pointing into other.geometry_
            if (handle_ != VK_NULL_HANDLE)
                buildInfo_.pGeometries = &geometry_;
        }
        return *this;
    }

    void AccelerationStructure::destroy() {
        scratchBuffer_.reset();
        if (handle_ != VK_NULL_HANDLE) {
            vkDestroyAccelerationStructureKHR(device_->device(), handle_, nullptr);
            buffer_.reset();
            handle_ = VK_NULL_HANDLE;
            deviceAddress_ = 0;
        }
    }
} // namespace Atlas
