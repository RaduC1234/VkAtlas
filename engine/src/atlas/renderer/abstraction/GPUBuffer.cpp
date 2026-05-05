#include "GPUBuffer.hpp"
#include "../Device.hpp"
#include <stdexcept>
#include <cassert>

namespace Atlas {
    GPUBuffer::GPUBuffer(Device &device,
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

    GPUBuffer::GPUBuffer(Device &device,
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

    GPUBuffer::~GPUBuffer() {
        destroy();
    }

    GPUBuffer::GPUBuffer(GPUBuffer &&other) noexcept
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

    GPUBuffer &GPUBuffer::operator=(GPUBuffer &&other) noexcept {
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

    void GPUBuffer::destroy() {
        if (mapped_) {
            unmap();
        }
        if (buffer_ != VK_NULL_HANDLE) {
            vmaDestroyBuffer(device_.allocator(), buffer_, allocation_);
            buffer_ = VK_NULL_HANDLE;
            allocation_ = nullptr;
        }
    }

    VkDeviceSize GPUBuffer::getAlignment(VkDeviceSize instanceSize, VkDeviceSize minOffsetAlignment) {
        if (minOffsetAlignment > 0) {
            return (instanceSize + minOffsetAlignment - 1) & ~(minOffsetAlignment - 1);
        }
        return instanceSize;
    }

    void GPUBuffer::map() {
        if (!mapped_) {
            if (vmaMapMemory(device_.allocator(), allocation_, &mapped_) != VK_SUCCESS) {
                throw std::runtime_error("Failed to map buffer memory!");
            }
        }
    }

    void GPUBuffer::unmap() {
        if (mapped_) {
            vmaUnmapMemory(device_.allocator(), allocation_);
            mapped_ = nullptr;
        }
    }

    void GPUBuffer::uploadData(const void *data, VkDeviceSize size, VkDeviceSize offset) {
        map();
        std::memcpy(static_cast<int8_t *>(mapped_) + offset, data, size);
        flush(size, offset);
    }

    void GPUBuffer::flush(VkDeviceSize size, VkDeviceSize offset) {
        if (size == VK_WHOLE_SIZE) {
            size = totalSize_ - offset;
        }
        if (size == 0) {
            return;
        }
        if (vmaFlushAllocation(device_.allocator(), allocation_, offset, size) != VK_SUCCESS) {
            throw std::runtime_error("Failed to flush buffer memory!");
        }
    }

    void GPUBuffer::writeToIndex(const void *data, int index) {
        assert(mapped_ && "Buffer must be mapped before writing");
        assert(index >= 0 && static_cast<uint32_t>(index) < instanceCount_);
        std::memcpy(static_cast<char *>(mapped_) + index * alignmentSize_, data, instanceSize_);
        flush(instanceSize_, index * alignmentSize_);
    }

    void GPUBuffer::flushIndex(int index) {
        flush(alignmentSize_, index * alignmentSize_);
    }

    VkDescriptorBufferInfo GPUBuffer::descriptorInfoForIndex(int index) const {
        VkDescriptorBufferInfo info{};
        info.buffer = buffer_;
        info.offset = index * alignmentSize_;
        info.range = alignmentSize_;
        return info;
    }

    VkDescriptorBufferInfo GPUBuffer::descriptorInfo() const {
        VkDescriptorBufferInfo info{};
        info.buffer = buffer_;
        info.offset = 0;
        info.range = alignmentSize_;
        return info;
    }

    void GPUBuffer::copy(Device &device, VkBuffer src, VkBuffer dst, VkDeviceSize size, VkDeviceSize srcOffset, VkDeviceSize dstOffset) {
        VkCommandBuffer commandBuffer = device.beginSingleTimeCommands();

        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = srcOffset;
        copyRegion.dstOffset = dstOffset;
        copyRegion.size = size;
        vkCmdCopyBuffer(commandBuffer, src, dst, 1, &copyRegion);

        device.endSingleTimeCommands(commandBuffer);
    }

    void GPUBuffer::copyToImage(Device &device, VkBuffer src, VkImage dst, VkImageLayout layout, const std::vector<VkBufferImageCopy> &regions) {
        VkCommandBuffer commandBuffer = device.beginSingleTimeCommands();

        vkCmdCopyBufferToImage(
            commandBuffer,
            src,
            dst,
            layout,
            static_cast<uint32_t>(regions.size()),
            regions.data()
        );

        device.endSingleTimeCommands(commandBuffer);
    }

    void GPUBuffer::copy(Device &device, VkBuffer src, VkImage dst, VkImageLayout layout, const std::vector<VkBufferImageCopy> &regions) {
        copyToImage(device, src, dst, layout, regions);
    }
} // namespace Atlas
