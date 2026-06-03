// Buffer.hpp
#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include "renderer/Device.hpp"

namespace Atlas {
    /**
     * Generic VMA-backed Vulkan buffer with support for suballocation, mapping, and dynamic descriptors.
     *
     * @code
     * // Simple buffer (vertex / index / static UBO):
     * Buffer vbo = Buffer::simple(device)
     *     .setSize(sizeof(vertices))
     *     .setUsage(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
     *     .build();
     *
     * // Instanced buffer (dynamic UBO):
     * Buffer ubo = Buffer::instanced(device)
     *     .setInstanceSize(sizeof(MyUBO))
     *     .setInstanceCount(MAX_FRAMES_IN_FLIGHT)
     *     .setMinOffsetAlignment(device.properties().limits.minUniformBufferOffsetAlignment)
     *     .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
     *     .setMapped()
     *     .build();
     * @endcode
     */
    class GPUBuffer {
    public:
        enum BuilderFlags : uint32_t {
            FLAG_MODE_SIMPLE = 1 << 0,
            FLAG_MODE_INSTANCED = 1 << 1,
            FLAG_SIZE = 1 << 2,
            FLAG_INSTANCE_SIZE = 1 << 3,
            FLAG_INSTANCE_COUNT = 1 << 4,
            FLAG_USAGE = 1 << 5,
            FLAG_MEMORY_USAGE = 1 << 6,
            FLAG_ALLOC_FLAGS = 1 << 7,
            FLAG_MAPPED = 1 << 8,
            FLAG_ALIGNMENT = 1 << 9,

            REQUIRED_SIMPLE = FLAG_MODE_SIMPLE | FLAG_SIZE | FLAG_USAGE,
            REQUIRED_INSTANCED = FLAG_MODE_INSTANCED | FLAG_INSTANCE_SIZE | FLAG_INSTANCE_COUNT | FLAG_USAGE,
        };

        template<uint32_t Flags = 0>
        class Builder {
        public:
            explicit Builder(Device &device) : device_(device) {
            }

            /**
             * [Simple] Total buffer size in bytes.
             * Calling this on an instanced-mode builder is a compile error.
             */
            auto setSize(VkDeviceSize size) const -> Builder<Flags | FLAG_MODE_SIMPLE | FLAG_SIZE> {
                static_assert(!(Flags & FLAG_MODE_INSTANCED), "[Buffer::Builder] setSize() is for simple-mode buffers. Use setInstanceSize() + setInstanceCount() instead.");
                Builder<Flags | FLAG_MODE_SIMPLE | FLAG_SIZE> next(device_);
                next.copyFrom(*this);
                next.size_ = size;
                return next;
            }

            /**
             * [Instanced] Size of one instance, e.g. sizeof(MyUBO).
             * Calling this on a simple-mode builder is a compile error.
             */
            auto setInstanceSize(VkDeviceSize instanceSize) const -> Builder<Flags | FLAG_MODE_INSTANCED | FLAG_INSTANCE_SIZE> {
                static_assert(!(Flags & FLAG_MODE_SIMPLE), "[Buffer::Builder] setInstanceSize() is for instanced-mode buffers. Use Buffer::instanced(device) or setSize() for simple buffers.");
                Builder<Flags | FLAG_MODE_INSTANCED | FLAG_INSTANCE_SIZE> next(device_);
                next.copyFrom(*this);
                next.instanceSize_ = instanceSize;
                return next;
            }

            /**
             * [Instanced] Number of instances to allocate.
             * Calling this on a simple-mode builder is a compile error.
             */
            auto setInstanceCount(uint32_t instanceCount) const -> Builder<Flags | FLAG_MODE_INSTANCED | FLAG_INSTANCE_COUNT> {
                static_assert(!(Flags & FLAG_MODE_SIMPLE), "[Buffer::Builder] setInstanceCount() is for instanced-mode buffers. Use Buffer::instanced(device).");
                Builder<Flags | FLAG_MODE_INSTANCED | FLAG_INSTANCE_COUNT> next(device_);
                next.copyFrom(*this);
                next.instanceCount_ = instanceCount;
                return next;
            }

            /**
             * [Instanced, optional] Minimum offset alignment in bytes.
             * Typically device.properties().limits.minUniformBufferOffsetAlignment. Defaults to 1.
             * Calling this on a simple-mode builder is a compile error.
             */
            auto setMinOffsetAlignment(VkDeviceSize alignment) const -> Builder<Flags | FLAG_MODE_INSTANCED | FLAG_ALIGNMENT> {
                static_assert(!(Flags & FLAG_MODE_SIMPLE), "[Buffer::Builder] setMinOffsetAlignment() is for instanced-mode buffers. Use Buffer::instanced(device).");
                Builder<Flags | FLAG_MODE_INSTANCED | FLAG_ALIGNMENT> next(device_);
                next.copyFrom(*this);
                next.minOffsetAlignment_ = alignment;
                return next;
            }

            /**
             * Vulkan buffer usage flags, e.g. VK_BUFFER_USAGE_VERTEX_BUFFER_BIT.
             */
            auto setUsage(VkBufferUsageFlags usage) const -> Builder<Flags | FLAG_USAGE> {
                Builder<Flags | FLAG_USAGE> next(device_);
                next.copyFrom(*this);
                next.usage_ = usage;
                return next;
            }

            /**
             * VMA memory usage policy. Defaults to VMA_MEMORY_USAGE_AUTO.
             */
            auto setMemoryUsage(VmaMemoryUsage memoryUsage) const -> Builder<Flags | FLAG_MEMORY_USAGE> {
                Builder<Flags | FLAG_MEMORY_USAGE> next(device_);
                next.copyFrom(*this);
                next.memoryUsage_ = memoryUsage;
                return next;
            }

            /**
             * VMA allocation create flags.
             */
            auto setAllocationFlags(VmaAllocationCreateFlags flags) const -> Builder<Flags | FLAG_ALLOC_FLAGS> {
                Builder<Flags | FLAG_ALLOC_FLAGS> next(device_);
                next.copyFrom(*this);
                next.allocFlags_ = flags;
                return next;
            }

            /**
             * Persistently map the buffer after creation. Required for CPU-visible buffers written every frame.
             */
            auto setMapped(bool mapped = true) const -> Builder<Flags | FLAG_MAPPED> {
                Builder<Flags | FLAG_MAPPED> next(device_);
                next.copyFrom(*this);
                next.mapped_ = mapped;
                return next;
            }

            /**
             * Constructs the Buffer. Only compiles when all required setters have been called.
             *
             * Simple mode requires:    setSize() + setUsage()
             * Instanced mode requires: setInstanceSize() + setInstanceCount() + setUsage()
             */
            GPUBuffer build() const
                requires (((Flags & REQUIRED_SIMPLE) == REQUIRED_SIMPLE) || ((Flags & REQUIRED_INSTANCED) == REQUIRED_INSTANCED)) {
                if constexpr (Flags & FLAG_MODE_SIMPLE) {
                    GPUBuffer buf(device_, size_, usage_, memoryUsage_, allocFlags_);
                    if (mapped_) {
                        buf.map();
                    }
                    return buf;
                } else {
                    GPUBuffer buf(device_, instanceSize_, instanceCount_, usage_, memoryUsage_, minOffsetAlignment_, allocFlags_);
                    if (mapped_) {
                        buf.map();
                    }
                    return buf;
                }
            }

        private:
            template<uint32_t>
            friend class Builder;

            template<uint32_t OtherFlags>
            void copyFrom(const Builder<OtherFlags> &o) {
                size_ = o.size_;
                instanceSize_ = o.instanceSize_;
                instanceCount_ = o.instanceCount_;
                minOffsetAlignment_ = o.minOffsetAlignment_;
                usage_ = o.usage_;
                memoryUsage_ = o.memoryUsage_;
                allocFlags_ = o.allocFlags_;
                mapped_ = o.mapped_;
            }

            Device &device_;
            VkDeviceSize size_ = 0;
            VkDeviceSize instanceSize_ = 0;
            uint32_t instanceCount_ = 1;
            VkDeviceSize minOffsetAlignment_ = 1;
            VkBufferUsageFlags usage_ = 0;
            VmaMemoryUsage memoryUsage_ = VMA_MEMORY_USAGE_AUTO;
            VmaAllocationCreateFlags allocFlags_ = 0;
            bool mapped_ = false;
        };

        /**
         * @brief Entry point for simple (size-based) buffers.
         */
        static Builder<FLAG_MODE_SIMPLE> simple(Device &device) { return Builder<FLAG_MODE_SIMPLE>(device); }

        /**
         * @brief Entry point for instanced (aligned, suballocated) buffers.
         */
        static Builder<FLAG_MODE_INSTANCED> instanced(Device &device) { return Builder<FLAG_MODE_INSTANCED>(device); }

        /**
         * @deprecated Use Buffer::instanced(device)...build()
         * @brief Constructor for aligned, suballocated buffers (e.g. dynamic UBOs).
         * @param device The Vulkan device wrapper.
         * @param instanceSize Size of one instance (e.g., sizeof(MyUBO)).
         * @param instanceCount How many instances to allocate.
         * @param usage Vulkan buffer usage flags (e.g., UNIFORM_BUFFER, TRANSFER_SRC).
         * @param memoryUsage VMA memory usage policy (e.g., CPU_TO_GPU, GPU_ONLY).
         * @param minOffsetAlignment Required alignment (usually from device.limits.minUniformBufferOffsetAlignment).
         * @param flags Optional VMA allocation flags.
         */
        GPUBuffer(Device &device,
               VkDeviceSize instanceSize,
               uint32_t instanceCount,
               VkBufferUsageFlags usage,
               VmaMemoryUsage memoryUsage,
               VkDeviceSize minOffsetAlignment = 1,
               VmaAllocationCreateFlags flags = 0);

        /**
         * @deprecated Use Buffer::simple(device)...build()
         * @brief Constructor for simple buffers (vertex/index/static UBOs).
         * @param device The Vulkan device wrapper.
         * @param size Total buffer size in bytes.
         * @param usage Vulkan buffer usage flags.
         * @param memoryUsage VMA memory usage policy.
         * @param flags Optional VMA allocation flags.
         */
        GPUBuffer(Device &device,
               VkDeviceSize size,
               VkBufferUsageFlags usage,
               VmaMemoryUsage memoryUsage,
               VmaAllocationCreateFlags flags = 0);

        ~GPUBuffer();

        GPUBuffer(const GPUBuffer &) = delete;
        GPUBuffer &operator=(const GPUBuffer &) = delete;
        GPUBuffer(GPUBuffer &&other) noexcept;
        GPUBuffer &operator=(GPUBuffer &&other) noexcept;

        /**
         * @brief Maps the buffer to CPU-visible memory. No-op if already mapped.
         */
        void map();

        /**
         * @brief Unmaps the buffer if previously mapped.
         */
        void unmap();

        /**
         * @brief Copies @p size bytes from @p data into the buffer at byte @p offset. Maps automatically if needed.
         * @param data Pointer to the source data.
         * @param size Number of bytes to copy.
         * @param offset Byte offset into the buffer.
         */
        void uploadData(const void *data, VkDeviceSize size, VkDeviceSize offset = 0);

        /**
         * @brief Flushes a mapped allocation range so host writes become visible to the GPU.
         * @param size Number of bytes to flush. VK_WHOLE_SIZE flushes to the end of the buffer.
         * @param offset Byte offset into the allocation.
         */
        void flush(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);

        /**
         * @brief Invalidates a mapped allocation range so GPU writes become visible to the host.
         * @param size Number of bytes to invalidate. VK_WHOLE_SIZE invalidates to the end of the buffer.
         * @param offset Byte offset into the allocation.
         */
        void invalidate(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);

        /**
         * @brief Copies one instance of data into the aligned slot at @p index. Buffer must be mapped.
         * @param data Pointer to a single instance.
         * @param index Index within the buffer (e.g., object index).
         */
        void writeToIndex(const void *data, int index);

        /**
         * @brief Flushes the aligned memory range for instance @p index. Required for non-coherent memory.
         * @param index Instance index to flush.
         */
        void flushIndex(int index);

        /**
         * @brief Returns a VkDescriptorBufferInfo for the aligned slot at @p index. Use for dynamic UBO bindings.
         * @param index The instance index.
         * @return Descriptor buffer info with offset and range for that instance.
         */
        VkDescriptorBufferInfo descriptorInfoForIndex(int index) const;

        /**
         * @brief Returns a VkDescriptorBufferInfo covering the entire buffer. Use for whole-buffer descriptor writes.
         */
        VkDescriptorBufferInfo descriptorInfo() const;

        /**
         * @brief Returns the raw VkBuffer handle.
         */
        VkBuffer get() const { return buffer_; }

        /**
         * @brief Returns the mapped pointer (if mapped).
         */
        void *getMapped() const { return mapped_; }

        /**
         * @brief GPU buffer-to-buffer copy via a transient command buffer.
         * @param device The Vulkan device wrapper.
         * @param src Source buffer.
         * @param dst Destination buffer.
         * @param size Number of bytes to copy.
         * @param srcOffset Byte offset in the source buffer.
         * @param dstOffset Byte offset in the destination buffer.
         */
        static void copy(Device &device, VkBuffer src, VkBuffer dst, VkDeviceSize size, VkDeviceSize srcOffset = 0, VkDeviceSize dstOffset = 0);

        /**
         * @brief GPU buffer-to-image copy via a transient command buffer.
         */
        static void copy(Device &device, VkBuffer src, VkImage dst, VkImageLayout layout, const std::vector<VkBufferImageCopy> &regions);

        /**
         * @brief Copy from a buffer to an image using provided regions.
         * @param device Device wrapper.
         * @param src Source buffer.
         * @param dst Destination image.
         * @param layout Destination image layout during copy.
         * @param regions Array of VkBufferImageCopy regions.
         */
        static void copyToImage(Device &device, VkBuffer src, VkImage dst, VkImageLayout layout, const std::vector<VkBufferImageCopy> &regions);

    private:
        template<uint32_t>
        friend class Builder;

        static VkDeviceSize getAlignment(VkDeviceSize instanceSize, VkDeviceSize minOffsetAlignment);
        void destroy();

        Device &device_;
        VkBuffer buffer_ = VK_NULL_HANDLE;
        VmaAllocation allocation_ = nullptr;
        VmaAllocationInfo allocInfo_ = {};
        void *mapped_ = nullptr;
        VkDeviceSize instanceSize_ = 0;
        VkDeviceSize alignmentSize_ = 0;
        uint32_t instanceCount_ = 1;
        VkDeviceSize totalSize_ = 0;
        VkBufferCreateInfo bufferInfo_ = {};
        VmaAllocationCreateInfo allocInfoCreate_ = {};
    };
} // namespace Atlas
