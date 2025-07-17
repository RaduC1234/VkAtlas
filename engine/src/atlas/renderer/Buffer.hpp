#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <memory>

#include "Device.hpp"

namespace Atlas {

    class Buffer {
    public:
        Buffer(Device &device, VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags flags = 0);
        ~Buffer();

        Buffer(const Buffer &) = delete;
        Buffer &operator=(const Buffer &) = delete;
        Buffer(Buffer &&other) noexcept;
        Buffer &operator=(Buffer &&other) noexcept;

        void destroy();
        void uploadData(const void *data, VkDeviceSize size) const;

        VkBuffer &get() { return buffer_; }
        VkDeviceSize size() const { return size_; }
        VmaAllocation allocation() const { return allocation_; }

        static void copy(Device &device, const VkBuffer &src, const VkBuffer &dest, const VkDeviceSize &size);

    private:
        Device &device_;
        VkBuffer buffer_{VK_NULL_HANDLE};
        VmaAllocation allocation_{nullptr};
        VmaAllocationInfo allocInfo_{};
        VkDeviceSize size_{0};

        VkBufferCreateInfo bufferInfo_{};
        VmaAllocationCreateInfo allocInfoCreate_{};
    };

}
