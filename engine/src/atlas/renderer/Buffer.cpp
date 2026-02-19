#include "Buffer.hpp"
#include "Device.hpp"
#include <stdexcept>
#include <cstring>
#include <cassert>

namespace Atlas {
    Buffer::Buffer(Device &device,
                   VkDeviceSize instanceSize,
                   uint32_t instanceCount,
                   VkBufferUsageFlags usage,
                   VmaMemoryUsage memoryUsage,
                   VkDeviceSize minOffsetAlignment,
                   VmaAllocationCreateFlags flags)
        : device_(device),
          instanceSize_(instanceSize),
          instanceCount_(instanceCount) {
        alignmentSize_ = getAlignment(instanceSize, minOffsetAlignment);
        totalSize_ = alignmentSize_ * instanceCount;

        bufferInfo_.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo_.size = totalSize_;
        bufferInfo_.usage = usage;
        bufferInfo_.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        allocInfoCreate_.usage = memoryUsage;
        allocInfoCreate_.flags = flags;

        if (vmaCreateBuffer(device_.allocator(), &bufferInfo_, &allocInfoCreate_, &buffer_, &allocation_, &allocInfo_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create buffer!");
        }
    }

    Buffer::Buffer(Device &device,
                   VkDeviceSize size,
                   VkBufferUsageFlags usage,
                   VmaMemoryUsage memoryUsage,
                   VmaAllocationCreateFlags flags)
        : device_(device), instanceSize_(size), alignmentSize_(size), totalSize_(size) {
        bufferInfo_.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo_.size = size;
        bufferInfo_.usage = usage;
        bufferInfo_.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        allocInfoCreate_.usage = memoryUsage;
        allocInfoCreate_.flags = flags;

        if (vmaCreateBuffer(device_.allocator(), &bufferInfo_, &allocInfoCreate_, &buffer_, &allocation_, &allocInfo_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create buffer!");
        }
    }

    Buffer::~Buffer() {
        destroy();
    }

    Buffer::Buffer(Buffer &&other) noexcept
        : device_(other.device_),
          buffer_(other.buffer_),
          allocation_(other.allocation_),
          allocInfo_(other.allocInfo_),
          mapped_(other.mapped_),
          instanceSize_(other.instanceSize_),
          alignmentSize_(other.alignmentSize_),
          instanceCount_(other.instanceCount_),
          totalSize_(other.totalSize_),
          bufferInfo_(other.bufferInfo_),
          allocInfoCreate_(other.allocInfoCreate_) {
        other.buffer_ = VK_NULL_HANDLE;
        other.allocation_ = nullptr;
        other.mapped_ = nullptr;
    }

    Buffer &Buffer::operator=(Buffer &&other) noexcept {
        if (this != &other) {
            destroy();

            buffer_ = other.buffer_;
            allocation_ = other.allocation_;
            allocInfo_ = other.allocInfo_;
            mapped_ = other.mapped_;
            instanceSize_ = other.instanceSize_;
            alignmentSize_ = other.alignmentSize_;
            instanceCount_ = other.instanceCount_;
            totalSize_ = other.totalSize_;
            bufferInfo_ = other.bufferInfo_;
            allocInfoCreate_ = other.allocInfoCreate_;

            other.buffer_ = VK_NULL_HANDLE;
            other.allocation_ = nullptr;
            other.mapped_ = nullptr;
        }
        return *this;
    }

    void Buffer::destroy() {
        if (mapped_) {
            unmap();
        }
        if (buffer_ != VK_NULL_HANDLE) {
            vmaDestroyBuffer(device_.allocator(), buffer_, allocation_);
            buffer_ = VK_NULL_HANDLE;
            allocation_ = nullptr;
        }
    }

    VkDeviceSize Buffer::getAlignment(VkDeviceSize instanceSize, VkDeviceSize minOffsetAlignment) {
        if (minOffsetAlignment > 0) {
            return (instanceSize + minOffsetAlignment - 1) & ~(minOffsetAlignment - 1);
        }
        return instanceSize;
    }

    void Buffer::map() {
        if (!mapped_) {
            if (vmaMapMemory(device_.allocator(), allocation_, &mapped_) != VK_SUCCESS) {
                throw std::runtime_error("Failed to map buffer memory!");
            }
        }
    }

    void Buffer::unmap() {
        if (mapped_) {
            vmaUnmapMemory(device_.allocator(), allocation_);
            mapped_ = nullptr;
        }
    }

    void Buffer::uploadData(const void *data, VkDeviceSize size, VkDeviceSize offset) {
        map();
        std::memcpy(static_cast<int8_t *>(mapped_) + offset, data, size);
        // Optional: flush if needed for non-coherent memory
    }

    void Buffer::writeToIndex(const void *data, int index) {
        assert(mapped_ && "Buffer must be mapped before writing");
        assert(index >= 0 && static_cast<uint32_t>(index) < instanceCount_);
        std::memcpy(static_cast<char *>(mapped_) + index * alignmentSize_, data, instanceSize_);
    }

    void Buffer::flushIndex(int index) {
        VkMappedMemoryRange range{};
        range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        range.memory = allocInfo_.deviceMemory;
        range.offset = allocInfo_.offset + index * alignmentSize_;
        range.size = alignmentSize_;
        vkFlushMappedMemoryRanges(device_.device(), 1, &range);
    }

    VkDescriptorBufferInfo Buffer::descriptorInfoForIndex(int index) const {
        VkDescriptorBufferInfo info{};
        info.buffer = buffer_;
        info.offset = index * alignmentSize_;
        info.range = alignmentSize_;
        return info;
    }

    VkDescriptorBufferInfo Buffer::descriptorInfo() const {
        VkDescriptorBufferInfo info{};
        info.buffer = buffer_;
        info.offset = 0;
        info.range = alignmentSize_;
        return info;
    }

    void Buffer::copy(Device &device, VkBuffer src, VkBuffer dst, VkDeviceSize size, VkDeviceSize srcOffset, VkDeviceSize dstOffset) {
        VkCommandBuffer commandBuffer = device.beginSingleTimeCommands();

        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = srcOffset;
        copyRegion.dstOffset = dstOffset;
        copyRegion.size = size;
        vkCmdCopyBuffer(commandBuffer, src, dst, 1, &copyRegion);

        device.endSingleTimeCommands(commandBuffer);
    }
} // namespace Atlas
