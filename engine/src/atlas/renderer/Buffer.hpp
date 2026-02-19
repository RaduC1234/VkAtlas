// Buffer.hpp
#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <cstdint>

#include "Device.hpp"

namespace Atlas {
/**
 * @class Buffer
 * @brief Generic VMA-backed Vulkan buffer with support for suballocation, mapping, and dynamic descriptors.
 *
 * Supports both simple buffers (e.g., vertex/index) and aligned instance-based buffers (e.g., dynamic UBOs).
 */
class Buffer {
public:
    /**
     * @brief Constructor for aligned, suballocated buffers (e.g. dynamic UBOs).
     * @param device The Vulkan device wrapper.
     * @param instanceSize Size of one instance (e.g., sizeof(MyUBO)).
     * @param instanceCount How many instances to allocate.
     * @param usage Vulkan buffer usage flags (e.g., UNIFORM_BUFFER, TRANSFER_SRC).
     * @param memoryUsage VMA memory usage policy (e.g., CPU_TO_GPU, GPU_ONLY).
     * @param minOffsetAlignment Required alignment (usually from device.limits.minUniformBufferOffsetAlignment).
     * @param flags Optional VMA allocation flags.
     */
    Buffer(Device& device,
           VkDeviceSize instanceSize,
           uint32_t instanceCount,
           VkBufferUsageFlags usage,
           VmaMemoryUsage memoryUsage,
           VkDeviceSize minOffsetAlignment = 1,
           VmaAllocationCreateFlags flags = 0);

    /**
     * @brief Constructor for simple buffers (vertex/index/static UBOs).
     * @param device The Vulkan device wrapper.
     * @param size Total buffer size in bytes.
     * @param usage Vulkan buffer usage flags.
     * @param memoryUsage VMA memory usage policy.
     * @param flags Optional VMA allocation flags.
     */
    Buffer(Device& device,
           VkDeviceSize size,
           VkBufferUsageFlags usage,
           VmaMemoryUsage memoryUsage,
           VmaAllocationCreateFlags flags = 0);

    ~Buffer();

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;

    /**
     * @brief Maps the buffer to CPU-visible memory.
     * Call once before writing, and unmap() when done.
     */
    void map();

    /**
     * @brief Unmaps the buffer if previously mapped.
     */
    void unmap();

    /**
     * @brief Uploads raw data to the buffer.
     * @param data Pointer to the source data.
     * @param size Number of bytes to copy.
     * @param offset Byte offset into the buffer.
     */
    void uploadData(const void* data, VkDeviceSize size, VkDeviceSize offset = 0);

    /**
     * @brief Writes data to a specific instance index (aligned).
     * @param data Pointer to a single instance.
     * @param index Index within the buffer (e.g., object index).
     */
    void writeToIndex(const void* data, int index);

    /**
     * @brief Flushes a memory range for the specified instance (needed for non-coherent memory).
     * @param index Instance index to flush.
     */
    void flushIndex(int index);

    /**
     * @brief Returns a VkDescriptorBufferInfo for the given index (used in dynamic UBOs).
     * @param index The instance index.
     * @return Descriptor buffer info with offset and range for that instance.
     */
    VkDescriptorBufferInfo descriptorInfoForIndex(int index) const;

    VkDescriptorBufferInfo descriptorInfo() const;

    /**
     * @brief Returns the raw VkBuffer handle.
     */
    VkBuffer get() const { return buffer_; }

    /**
     * @brief Returns the mapped pointer (if mapped).
     */
    void* getMapped() const { return mapped_; }

    /**
     * @brief Static helper to copy from one buffer to another using a command buffer.
     * @param device The Vulkan device wrapper.
     * @param src Source buffer.
     * @param dst Destination buffer.
     * @param size Number of bytes to copy.
     * @param srcOffset Byte offset in the source buffer.
     * @param dstOffset Byte offset in the destination buffer.
     */
    static void copy(Device &device, VkBuffer src, VkBuffer dst, VkDeviceSize size, VkDeviceSize srcOffset = 0, VkDeviceSize dstOffset = 0);

private:
    /**
     * @brief Computes aligned size based on hardware alignment requirements.
     */
    static VkDeviceSize getAlignment(VkDeviceSize instanceSize, VkDeviceSize minOffsetAlignment);

    void destroy();

    Device& device_;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = nullptr;
    VmaAllocationInfo allocInfo_{};

    void* mapped_ = nullptr;

    VkDeviceSize instanceSize_ = 0;
    VkDeviceSize alignmentSize_ = 0;
    uint32_t instanceCount_ = 1;
    VkDeviceSize totalSize_ = 0;

    VkBufferCreateInfo bufferInfo_{};
    VmaAllocationCreateInfo allocInfoCreate_{};
};

} // namespace Atlas
