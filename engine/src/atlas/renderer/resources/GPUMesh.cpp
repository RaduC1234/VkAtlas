#include "GPUMesh.hpp"

#include <stdexcept>

#include "asset/Mesh.hpp"

namespace Atlas {
    std::unique_ptr<GPUMesh> GPUMesh::createDefault(Device &device) {
        auto resource = std::unique_ptr<GPUMesh>(new GPUMesh(device));
        resource->setStatus(Status::READY);
        return resource;
    }

    GPUMesh::GPUMesh(Device &device) : IGPUResource(Type::MESH), device_(device) {
    }

    // -------------------------------------------------------------------------
    // Constructor — Phase 1, pure CPU, no command recording
    // -------------------------------------------------------------------------

    GPUMesh::GPUMesh(Device &device, const Mesh &mesh) : IGPUResource(Type::MESH), device_(device), vertexCount_(static_cast<uint32_t>(mesh.vertices().size())), indexCount_(static_cast<uint32_t>(mesh.indices().size())) {
        assert(vertexCount_ >= 3 && "GPUMesh: vertex count must be at least 3");

        const VkDeviceSize vertexSize = sizeof(Mesh::Vertex) * vertexCount_;
        const VkDeviceSize indexSize = sizeof(uint32_t) * indexCount_;

        // Allocate device-local vertex buffer — no staging yet
        vertexBuffer_ = std::make_unique<GPUBuffer>(
            GPUBuffer::simple(device_)
            .setSize(vertexSize)
            .setUsage(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
                      | VK_BUFFER_USAGE_TRANSFER_DST_BIT
                      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
                      | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
                      | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
            .setMemoryUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
            .build());

        // Allocate device-local index buffer
        indexBuffer_ = std::make_unique<GPUBuffer>(
            GPUBuffer::simple(device_)
            .setSize(indexSize)
            .setUsage(VK_BUFFER_USAGE_INDEX_BUFFER_BIT
                      | VK_BUFFER_USAGE_TRANSFER_DST_BIT
                      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
                      | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
                      | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
            .setMemoryUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
            .build());

        // Fill staging buffers — memcpy into host memory, no GPU work
        vertexStaging_ = std::make_unique<GPUBuffer>(
            GPUBuffer::simple(device_)
            .setSize(vertexSize)
            .setUsage(VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
            .setMemoryUsage(VMA_MEMORY_USAGE_AUTO)
            .setAllocationFlags(
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                VMA_ALLOCATION_CREATE_MAPPED_BIT)
            .build());

        vertexStaging_->map();
        vertexStaging_->uploadData(mesh.vertices().data(), vertexSize);
        vertexStaging_->unmap();

        indexStaging_ = std::make_unique<GPUBuffer>(
            GPUBuffer::simple(device_)
            .setSize(indexSize)
            .setUsage(VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
            .setMemoryUsage(VMA_MEMORY_USAGE_AUTO)
            .setAllocationFlags(
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                VMA_ALLOCATION_CREATE_MAPPED_BIT)
            .build());

        indexStaging_->map();
        indexStaging_->uploadData(mesh.indices().data(), indexSize);
        indexStaging_->unmap();

        // Allocate BLAS handle + scratch buffer — device queries only, no cmds
        // Buffer addresses are valid immediately after vmaCreateBuffer
        blas_ = AccelerationStructure::allocateBLAS(
            device_,
            vertexBufferAddress(),
            indexBufferAddress(),
            vertexCount_,
            indexCount_,
            sizeof(Mesh::Vertex));

        setStatus(Status::PENDING_UPLOAD);
    }

    GPUMesh::~GPUMesh() {
        vertexStaging_.reset();
        indexStaging_.reset();
        // blas_ destructs via AccelerationStructure::~AccelerationStructure
        // vertexBuffer_ and indexBuffer_ destruct via GPUBuffer destructor
    }

    // -------------------------------------------------------------------------
    // Phase 2 — record into shared transfer cmd buffer
    // -------------------------------------------------------------------------

    void GPUMesh::recordTransfer(VkCommandBuffer cmd) {
        if (vertexCount_ == 0) return;

        // Copy vertex data: staging → device-local
        VkBufferCopy vertexCopy{};
        vertexCopy.size = sizeof(Mesh::Vertex) * vertexCount_;
        vkCmdCopyBuffer(cmd, vertexStaging_->get(), vertexBuffer_->get(), 1, &vertexCopy);

        // Copy index data: staging → device-local
        VkBufferCopy indexCopy{};
        indexCopy.size = sizeof(uint32_t) * indexCount_;
        vkCmdCopyBuffer(cmd, indexStaging_->get(), indexBuffer_->get(), 1, &indexCopy);

        // Barrier — copy must complete before BLAS build reads the buffers
        VkMemoryBarrier copyBarrier{};
        copyBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        copyBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        copyBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                             0, 1, &copyBarrier, 0, nullptr, 0, nullptr);

        // Record BLAS build into same cmd buffer — no submit
        blas_.recordBuild(cmd);

    }

    // -------------------------------------------------------------------------
    // Phase 3 — transfer completion callback, no GPU calls
    // -------------------------------------------------------------------------

    void GPUMesh::onTransferComplete() {
        vertexStaging_.reset(); // CPU vertex data freed
        indexStaging_.reset(); // CPU index data freed
        blas_.onBuildComplete(); // scratch buffer freed
        // Status set to READY by ResourceManager::markAcquiredReady()
        // after the acquire barrier executes on the graphics queue
    }

    // -------------------------------------------------------------------------
    // Phase 4 — ownership acquire barrier, graphics queue, frame start
    // -------------------------------------------------------------------------

    void GPUMesh::recordOwnershipAcquire(VkCommandBuffer cmd) {
        if (vertexCount_ == 0) return;

        const auto &f = device_.queueFamilyIndices();

        // Both vertex and index buffer barriers in one call
        VkBufferMemoryBarrier barriers[2]{};
        for (auto &b: barriers) {
            b.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            b.srcAccessMask = 0;
            b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT
                              | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT
                              | VK_ACCESS_INDEX_READ_BIT;
            // Same family → srcQueueFamilyIndex == dstQueueFamilyIndex
            // driver treats as plain memory barrier, no QFOT performed
            b.srcQueueFamilyIndex = f.transferFamily.value();
            b.dstQueueFamilyIndex = f.graphicsFamily.value();
            b.offset = 0;
            b.size = VK_WHOLE_SIZE;
        }
        barriers[0].buffer = vertexBuffer_->get();
        barriers[1].buffer = indexBuffer_->get();

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_VERTEX_INPUT_BIT
                             | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT
                             | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                             0,
                             0, nullptr,
                             2, barriers,
                             0, nullptr);
    }

    void GPUMesh::bind(VkCommandBuffer cmd) const {
        if (vertexCount_ == 0) return;

        VkBuffer buffers[] = {vertexBuffer_->get()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);

        if (indexCount_ > 0)
            vkCmdBindIndexBuffer(cmd, indexBuffer_->get(), 0, VK_INDEX_TYPE_UINT32);
    }

    void GPUMesh::draw(VkCommandBuffer cmd) const {
        if (vertexCount_ == 0) return;

        if (indexCount_ > 0)
            vkCmdDrawIndexed(cmd, indexCount_, 1, 0, 0, 0);
        else
            vkCmdDraw(cmd, vertexCount_, 1, 0, 0);
    }

    VkDeviceAddress GPUMesh::vertexBufferAddress() const {
        VkBufferDeviceAddressInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        info.buffer = vertexBuffer_->get();
        return vkGetBufferDeviceAddress(device_.device(), &info);
    }

    VkDeviceAddress GPUMesh::indexBufferAddress() const {
        VkBufferDeviceAddressInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        info.buffer = indexBuffer_->get();
        return vkGetBufferDeviceAddress(device_.device(), &info);
    }

    void GPUMesh::recordOwnershipRelease(VkCommandBuffer cmd) {
        if (vertexCount_ == 0) return;

        const auto &f = device_.queueFamilyIndices();

        VkBufferMemoryBarrier barriers[2]{};
        for (auto &b: barriers) {
            b.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            b.dstAccessMask = 0;
            b.srcQueueFamilyIndex = f.transferFamily.value();
            b.dstQueueFamilyIndex = f.graphicsFamily.value();
            b.offset = 0;
            b.size = VK_WHOLE_SIZE;
        }
        barriers[0].buffer = vertexBuffer_->get();
        barriers[1].buffer = indexBuffer_->get();

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                             0,
                             0, nullptr,
                             2, barriers,
                             0, nullptr);
    }

    std::vector<VkVertexInputBindingDescription> GPUMesh::Vertex::getBindingDescriptions() {
        return {{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX}};
    }

    std::vector<VkVertexInputAttributeDescription> GPUMesh::Vertex::getAttributeDescriptions() {
        return {
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
            {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)},
            {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
            {3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)},
            {4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, tangent)},
        };
    }
}
