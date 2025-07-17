#include "Buffer.hpp"
#include <stdexcept>
#include <cstring>

namespace Atlas {
    Buffer::Buffer(Device &device,
                   VkDeviceSize size,
                   VkBufferUsageFlags usage,
                   VmaMemoryUsage memoryUsage,
                   VmaAllocationCreateFlags flags)
        : device_(device), size_(size) {
        bufferInfo_.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo_.size = size;
        bufferInfo_.usage = usage;
        bufferInfo_.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        allocInfoCreate_.usage = memoryUsage;
        allocInfoCreate_.flags = flags;

        if (vmaCreateBuffer(device_.allocator(), &bufferInfo_, &allocInfoCreate_, &buffer_, &allocation_, &allocInfo_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create VMA buffer!");
        }
    }

    Buffer::~Buffer() {
        destroy();
    }

    Buffer::Buffer(Buffer &&other) noexcept
        : device_(other.device_),
          buffer_(other.buffer_),
          allocation_(other.allocation_),
          size_(other.size_),
          bufferInfo_(other.bufferInfo_),
          allocInfoCreate_(other.allocInfoCreate_),
          allocInfo_(other.allocInfo_) {
        other.buffer_ = VK_NULL_HANDLE;
        other.allocation_ = nullptr;
    }

    Buffer &Buffer::operator=(Buffer &&other) noexcept {
        if (this != &other) {
            destroy();

            buffer_ = other.buffer_;
            allocation_ = other.allocation_;
            size_ = other.size_;
            bufferInfo_ = other.bufferInfo_;
            allocInfoCreate_ = other.allocInfoCreate_;
            allocInfo_ = other.allocInfo_;

            other.buffer_ = VK_NULL_HANDLE;
            other.allocation_ = nullptr;
        }
        return *this;
    }

    void Buffer::destroy() {
        if (buffer_ != VK_NULL_HANDLE) {
            vmaDestroyBuffer(device_.allocator(), buffer_, allocation_);
            buffer_ = VK_NULL_HANDLE;
            allocation_ = nullptr;
        }
    }

    void Buffer::uploadData(const void *data, VkDeviceSize size) const {
        void *mapped;
        vmaMapMemory(device_.allocator(), allocation_, &mapped);
        std::memcpy(mapped, data, size);
        vmaUnmapMemory(device_.allocator(), allocation_);
    }

    void Buffer::copy(Device &device, const VkBuffer &src, const VkBuffer &dest, const VkDeviceSize &size) {
        auto commandBuffer = device.beginSingleTimeCommands();

        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = size;
        vkCmdCopyBuffer(commandBuffer, src, dest, 1, &copyRegion);

        device.endSingleTimeCommands(commandBuffer);
    }
}
