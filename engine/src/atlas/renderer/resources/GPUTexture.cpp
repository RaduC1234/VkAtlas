#include "GPUTexture.hpp"

#include <cmath>
#include <stdexcept>

#include "asset/Texture.hpp"
#include "renderer/abstraction/GPUBuffer.hpp"

namespace Atlas {
    GPUTexture::GPUTexture(Device &device, const Texture &texture) : IGPUResource(Type::TEXTURE), device_(device), width_(texture.width()), height_(texture.height()), format_(resolveFormat(texture.format())) {
        mipLevels_ = static_cast<uint32_t>(std::floor(std::log2(std::max(width_, height_)))) + 1;

        const VkDeviceSize size = static_cast<VkDeviceSize>(width_) * height_ * bytesPerPixel(format_);

        allocateImage();
        createImageView();
        createSampler(texture.addressMode());
        fillStagingBuffer(texture.pixels().data(), size);

        setStatus(Status::PENDING_UPLOAD);
    }

    GPUTexture::~GPUTexture() {
        stagingBuffer_.reset();
        if (sampler_ != VK_NULL_HANDLE) vkDestroySampler(device_.device(), sampler_, nullptr);
        if (imageView_ != VK_NULL_HANDLE) vkDestroyImageView(device_.device(), imageView_, nullptr);
        if (image_ != VK_NULL_HANDLE) vmaDestroyImage(device_.allocator(), image_, memory_);
    }

    void GPUTexture::recordTransfer(VkCommandBuffer cmd) {
        recordTransition(cmd, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        recordCopyBufferToImage(cmd);
        recordGenerateMipmaps(cmd);
    }

    void GPUTexture::onTransferComplete() {
        stagingBuffer_.reset();
        imageLayout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    void GPUTexture::recordOwnershipAcquire(VkCommandBuffer cmd) {
        const auto &f = device_.queueFamilyIndices();

        VkImageMemoryBarrier acquire{};
        acquire.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        acquire.srcAccessMask = 0;
        acquire.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        acquire.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        acquire.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        acquire.srcQueueFamilyIndex = f.transferFamily.value();
        acquire.dstQueueFamilyIndex = f.graphicsFamily.value();
        acquire.image = image_;
        acquire.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels_, 0, 1};

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &acquire);
    }

    void GPUTexture::updateBindlessSlot() {
        if (!bindlessSlot_) return;

        VkDescriptorImageInfo info = descriptor();
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = bindlessSlot_->set;
        write.dstBinding = bindlessSlot_->binding;
        write.dstArrayElement = bindlessSlot_->arrayElement;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &info;

        vkUpdateDescriptorSets(bindlessSlot_->device, 1, &write, 0, nullptr);
    }

    void GPUTexture::registerBindlessSlot(VkDevice device, VkDescriptorSet set, uint32_t binding, uint32_t arrayElement) {
        bindlessSlot_ = {device, set, binding, arrayElement};
    }

    std::unique_ptr<GPUTexture> GPUTexture::createDefault(Device &device) {
        constexpr uint8_t white[4] = {255, 255, 255, 255};

        std::vector<std::byte> pixels;
        pixels.assign(reinterpret_cast<const std::byte *>(white), reinterpret_cast<const std::byte *>(white) + 4);

        Texture texture(pixels, 1, 1, VK_FORMAT_R8G8B8A8_SRGB, VK_SAMPLER_ADDRESS_MODE_REPEAT);
        auto resource = std::make_unique<GPUTexture>(device, texture);

        VkCommandBuffer cmd = device.beginGraphicsCommands();
        resource->recordTransition(cmd, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        resource->recordCopyBufferToImage(cmd);
        resource->recordGenerateMipmaps(cmd);
        device.endGraphicsCommands(cmd);

        resource->onTransferComplete();
        resource->setStatus(Status::READY);

        return resource;
    }

    void GPUTexture::allocateImage() {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = {width_, height_, 1};
        imageInfo.mipLevels = mipLevels_;
        imageInfo.arrayLayers = 1;
        imageInfo.format = format_;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

        if (vmaCreateImage(device_.allocator(), &imageInfo, &allocInfo, &image_, &memory_, nullptr) != VK_SUCCESS)
            throw std::runtime_error("GPUTexture: failed to allocate image");
    }

    void GPUTexture::createImageView() {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image_;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format_;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels_, 0, 1};

        if (vkCreateImageView(device_.device(), &viewInfo, nullptr, &imageView_) != VK_SUCCESS)
            throw std::runtime_error("GPUTexture: failed to create image view");
    }

    void GPUTexture::createSampler(VkSamplerAddressMode addressMode) {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = addressMode;
        samplerInfo.addressModeV = addressMode;
        samplerInfo.addressModeW = addressMode;
        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = device_.properties.limits.maxSamplerAnisotropy;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = static_cast<float>(mipLevels_);

        if (vkCreateSampler(device_.device(), &samplerInfo, nullptr, &sampler_) != VK_SUCCESS)
            throw std::runtime_error("GPUTexture: failed to create sampler");
    }

    void GPUTexture::fillStagingBuffer(const void *pixels, VkDeviceSize size) {
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

    void GPUTexture::recordTransition(VkCommandBuffer cmd, VkImageLayout oldLayout, VkImageLayout newLayout) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image_;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels_, 0, 1};

        VkPipelineStageFlags srcStage, dstStage;

        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        } else {
            throw std::invalid_argument("GPUTexture: unsupported layout transition");
        }

        vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    void GPUTexture::recordCopyBufferToImage(VkCommandBuffer cmd) {
        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {width_, height_, 1};

        vkCmdCopyBufferToImage(cmd, stagingBuffer_->get(), image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    }

    void GPUTexture::recordGenerateMipmaps(VkCommandBuffer cmd) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image = image_;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        int32_t mipW = static_cast<int32_t>(width_);
        int32_t mipH = static_cast<int32_t>(height_);

        for (uint32_t i = 1; i < mipLevels_; ++i) {
            barrier.subresourceRange.baseMipLevel = i - 1;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &barrier);

            VkImageBlit blit{};
            blit.srcOffsets[0] = {0, 0, 0};
            blit.srcOffsets[1] = {mipW, mipH, 1};
            blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 0, 1};
            blit.dstOffsets[0] = {0, 0, 0};
            blit.dstOffsets[1] = {mipW > 1 ? mipW / 2 : 1, mipH > 1 ? mipH / 2 : 1, 1};
            blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1};

            vkCmdBlitImage(cmd,
                           image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &blit, VK_FILTER_LINEAR);

            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &barrier);

            if (mipW > 1) mipW /= 2;
            if (mipH > 1) mipH /= 2;
        }

        barrier.subresourceRange.baseMipLevel = mipLevels_ - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    void GPUTexture::recordOwnershipRelease(VkCommandBuffer cmd) {
        const auto &f = device_.queueFamilyIndices();

        VkImageMemoryBarrier release{};
        release.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        release.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        release.dstAccessMask = 0;
        release.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        release.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        release.srcQueueFamilyIndex = f.transferFamily.value();
        release.dstQueueFamilyIndex = f.graphicsFamily.value();
        release.image = image_;
        release.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels_, 0, 1};

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &release);
    }

    VkFormat GPUTexture::resolveFormat(VkFormat requested) {
        switch (requested) {
            case VK_FORMAT_R8G8B8_SRGB: return VK_FORMAT_R8G8B8A8_SRGB;
            case VK_FORMAT_R8G8B8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
            case VK_FORMAT_R32G32B32_SFLOAT: return VK_FORMAT_R32G32B32A32_SFLOAT;
            default: return requested;
        }
    }

    uint32_t GPUTexture::bytesPerPixel(VkFormat format) {
        switch (format) {
            case VK_FORMAT_R8_UNORM:
            case VK_FORMAT_R8_SNORM: return 1;
            case VK_FORMAT_R16G16_SFLOAT: return 4;
            case VK_FORMAT_R8G8B8A8_SRGB:
            case VK_FORMAT_R8G8B8A8_UNORM: return 4;
            case VK_FORMAT_R32G32_SFLOAT: return 8;
            case VK_FORMAT_R32G32B32A32_SFLOAT: return 16;
            default: return 4;
        }
    }
} // namespace Atlas
