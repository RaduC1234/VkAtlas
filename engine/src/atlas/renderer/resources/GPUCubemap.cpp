#include "GPUCubemap.hpp"

#include <algorithm>
#include <stdexcept>

#include "asset/Cubemap.hpp"
#include "renderer/abstraction/GPUBuffer.hpp"

namespace Atlas {
    std::unique_ptr<GPUCubemap> GPUCubemap::createDefault(Device &device) {
        constexpr uint8_t white[4] = {255, 255, 255, 255};

        std::vector<std::byte> pixels;
        pixels.reserve(4 * 6);
        for (uint32_t face = 0; face < 6; ++face) {
            pixels.insert(
                pixels.end(),
                reinterpret_cast<const std::byte *>(white),
                reinterpret_cast<const std::byte *>(white) + 4);
        }

        std::vector<VkBufferImageCopy> copyRegions;
        copyRegions.reserve(6);
        for (uint32_t face = 0; face < 6; ++face) {
            VkBufferImageCopy region{};
            region.bufferOffset = static_cast<VkDeviceSize>(face * 4);
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = face;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = {0, 0, 0};
            region.imageExtent = {1, 1, 1};
            copyRegions.push_back(region);
        }

        Cubemap cubemap(
            pixels,
            1,
            1,
            1,
            VK_FORMAT_R8G8B8A8_SRGB,
            copyRegions);

        auto resource = std::make_unique<GPUCubemap>(device, cubemap);

        VkCommandBuffer cmd = device.beginGraphicsCommands();
        resource->recordTransition(cmd, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        resource->recordCopyBufferToImage(cmd);
        resource->recordTransition(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        device.endGraphicsCommands(cmd);

        resource->onUploadComplete();
        resource->setStatus(Status::READY);

        return resource;
    }

    GPUCubemap::GPUCubemap(Device &device, const Cubemap &cubemap) : IGPUResource(Type::CUBEMAP), device_(device) {
        width_ = cubemap.width();
        height_ = cubemap.height();
        mipLevels_ = cubemap.mipLevels();
        format_ = cubemap.format();
        copyRegions_ = cubemap.copyRegions();

        const VkDeviceSize totalSize = static_cast<VkDeviceSize>(cubemap.pixels().size());

        allocateImage(format_);
        createImageView(format_);
        createSampler();
        fillStagingBuffer(cubemap.pixels().data(), totalSize);

        setStatus(Status::PENDING_UPLOAD);
    }

    GPUCubemap::~GPUCubemap() {
        stagingBuffer_.reset();
        if (sampler_ != VK_NULL_HANDLE) vkDestroySampler(device_.device(), sampler_, nullptr);
        if (imageView_ != VK_NULL_HANDLE) vkDestroyImageView(device_.device(), imageView_, nullptr);
        if (image_ != VK_NULL_HANDLE) vmaDestroyImage(device_.allocator(), image_, allocation_);
    }

    void GPUCubemap::recordUpload(VkCommandBuffer cmd) {
        recordTransition(cmd, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        recordCopyBufferToImage(cmd);
        recordTransition(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    void GPUCubemap::onUploadComplete() {
        stagingBuffer_.reset();
        copyRegions_.clear();
        imageLayout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    void GPUCubemap::updateBindlessSlot() {
        VkDescriptorImageInfo info{};
        info.sampler = sampler_;
        info.imageView = imageView_;
        info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        for (const auto &slot: bindlessSlots_) {
            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = slot.set;
            write.dstBinding = slot.binding;
            write.dstArrayElement = slot.arrayElement;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.descriptorCount = 1;
            write.pImageInfo = &info;

            vkUpdateDescriptorSets(slot.device, 1, &write, 0, nullptr);
        }
    }

    void GPUCubemap::registerBindlessSlot(VkDevice device, VkDescriptorSet set,
                                          uint32_t binding, uint32_t arrayElement) {
        const auto it = std::ranges::find_if(bindlessSlots_, [&](const BindlessSlot &slot) {
            return slot.device == device &&
                   slot.set == set &&
                   slot.binding == binding &&
                   slot.arrayElement == arrayElement;
        });

        if (it == bindlessSlots_.end()) {
            bindlessSlots_.push_back({device, set, binding, arrayElement});
        }
    }

    void GPUCubemap::allocateImage(VkFormat format) {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = {width_, height_, 1};
        imageInfo.mipLevels = mipLevels_;
        imageInfo.arrayLayers = 6;
        imageInfo.format = format;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

        if (vmaCreateImage(device_.allocator(), &imageInfo, &allocInfo, &image_, &allocation_, nullptr) != VK_SUCCESS)
            throw std::runtime_error("GPUCubemap: failed to allocate image");
    }

    void GPUCubemap::createImageView(VkFormat format) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image_;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        viewInfo.format = format;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels_, 0, 6};

        if (vkCreateImageView(device_.device(), &viewInfo, nullptr, &imageView_) != VK_SUCCESS)
            throw std::runtime_error("GPUCubemap: failed to create image view");
    }

    void GPUCubemap::createSampler() {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = device_.properties.limits.maxSamplerAnisotropy;
        samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = static_cast<float>(mipLevels_);

        if (vkCreateSampler(device_.device(), &samplerInfo, nullptr, &sampler_) != VK_SUCCESS)
            throw std::runtime_error("GPUCubemap: failed to create sampler");
    }

    void GPUCubemap::fillStagingBuffer(const void *pixels, VkDeviceSize size) {
        stagingBuffer_ = std::make_unique<GPUBuffer>(
            GPUBuffer::simple(device_)
            .setSize(size)
            .setUsage(VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
            .setMemoryUsage(VMA_MEMORY_USAGE_AUTO)
            .setAllocationFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT)
            .build());

        stagingBuffer_->map();
        stagingBuffer_->uploadData(pixels, size);
        stagingBuffer_->unmap();
    }

    void GPUCubemap::recordTransition(VkCommandBuffer cmd, VkImageLayout oldLayout, VkImageLayout newLayout) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image_;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels_, 0, 6};

        VkPipelineStageFlags srcStage, dstStage;

        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        } else {
            throw std::invalid_argument("GPUCubemap: unsupported layout transition");
        }

        vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    void GPUCubemap::recordCopyBufferToImage(VkCommandBuffer cmd) {
        vkCmdCopyBufferToImage(
            cmd,
            stagingBuffer_->get(),
            image_,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            static_cast<uint32_t>(copyRegions_.size()),
            copyRegions_.data());
    }

} // namespace Atlas
