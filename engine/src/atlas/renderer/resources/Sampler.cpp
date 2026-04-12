#include "Sampler.hpp"

#include <stb_image.h>

#include "renderer/Device.hpp"
#include "renderer/abstraction/Buffer.hpp"
#include "utils/Hash.hpp"

namespace std {
    template<>
    struct hash<Atlas::Sampler> {
        size_t operator()(const Atlas::Sampler &obj) const noexcept {
            return obj.getHash();
        }
    };
}

namespace Atlas {
    Sampler::Sampler(Device &device, uint32_t width, uint32_t height, const void *pixels, VkFormat format, VkSamplerAddressMode addressMode): device(device), width(width), height(height) {
        uint32_t bytesPerPixel;
        switch (format) {
            case VK_FORMAT_R8_UNORM:
            case VK_FORMAT_R8_SNORM:
                bytesPerPixel = 1;
                break;
            case VK_FORMAT_R32G32_SFLOAT:
                bytesPerPixel = 8;
                break;
            case VK_FORMAT_R32G32B32_SFLOAT:
                bytesPerPixel = 12;
                break;
            case VK_FORMAT_R32G32B32A32_SFLOAT:
                bytesPerPixel = 16;
                break;
            case VK_FORMAT_R16G16_SFLOAT:
                bytesPerPixel = 4;
                break;
            default:
                bytesPerPixel = 4;
                break;
        }

        const VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * bytesPerPixel;

        createTextureImage(pixels, imageSize, format);
        createTextureImageView();
        createTextureSampler(addressMode);
    }

    Sampler::~Sampler() {
        vkDestroySampler(device.device(), sampler, nullptr);
        vkDestroyImageView(device.device(), imageView, nullptr);
        vmaDestroyImage(device.allocator(), textureImage, textureImageMemory);
    }

    void Sampler::createTextureImage(const void *pixels, VkDeviceSize imageSize, VkFormat format) {
        VkFormat actualFormat = format;

        if (format == VK_FORMAT_R8G8B8_SRGB) {
            actualFormat = VK_FORMAT_R8G8B8A8_SRGB;
        } else if (format == VK_FORMAT_R8G8B8_UNORM) {
            actualFormat = VK_FORMAT_R8G8B8A8_UNORM;
        } else if (format == VK_FORMAT_R32G32B32_SFLOAT) {
            actualFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
        }

        // Store the actual format for use in createTextureImageView
        this->format = actualFormat;

        // Calculate full mip chain depth
        mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;

        Buffer stagingBuffer(
            device,
            imageSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
        );

        stagingBuffer.map();
        stagingBuffer.uploadData(pixels, imageSize);
        stagingBuffer.unmap();

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = this->width;
        imageInfo.extent.height = this->height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = mipLevels;
        imageInfo.arrayLayers = 1;
        imageInfo.format = actualFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        // TRANSFER_SRC_BIT is required for vkCmdBlitImage when generating mips
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.flags = 0;

        VmaAllocationCreateInfo imageAllocInfo{};
        imageAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        imageAllocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

        if (vmaCreateImage(device.allocator(), &imageInfo, &imageAllocInfo, &textureImage, &textureImageMemory, nullptr) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create texture image!");
        }

        // Transition base mip to DST_OPTIMAL, upload, then blit down the chain.
        // generateMipmaps handles the final transition to SHADER_READ_ONLY_OPTIMAL.
        transitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        copyBufferToImage(stagingBuffer.get(), textureImage, width, height);
        generateMipmaps(textureImage, actualFormat, width, height, mipLevels);
    }

    void Sampler::generateMipmaps(VkImage image, VkFormat /*format*/, uint32_t texWidth, uint32_t texHeight, uint32_t mipLevels) {
        VkCommandBuffer commandBuffer = device.beginSingleTimeCommands();

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image = image;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.subresourceRange.levelCount = 1;

        int32_t mipW = static_cast<int32_t>(texWidth);
        int32_t mipH = static_cast<int32_t>(texHeight);

        for (uint32_t i = 1; i < mipLevels; i++) {
            // Transition mip i-1: TRANSFER_DST -> TRANSFER_SRC so it can be blitted from
            barrier.subresourceRange.baseMipLevel = i - 1;
            barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(commandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &barrier);

            // Blit mip i-1 -> mip i (half resolution, linear filter)
            VkImageBlit blit{};
            blit.srcOffsets[0]                 = {0, 0, 0};
            blit.srcOffsets[1]                 = {mipW, mipH, 1};
            blit.srcSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.mipLevel       = i - 1;
            blit.srcSubresource.baseArrayLayer = 0;
            blit.srcSubresource.layerCount     = 1;
            blit.dstOffsets[0]                 = {0, 0, 0};
            blit.dstOffsets[1]                 = {mipW > 1 ? mipW / 2 : 1, mipH > 1 ? mipH / 2 : 1, 1};
            blit.dstSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.dstSubresource.mipLevel       = i;
            blit.dstSubresource.baseArrayLayer = 0;
            blit.dstSubresource.layerCount     = 1;

            vkCmdBlitImage(commandBuffer,
                image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, &blit, VK_FILTER_LINEAR);

            // Transition mip i-1 to its final SHADER_READ_ONLY layout
            barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(commandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &barrier);

            if (mipW > 1) mipW /= 2;
            if (mipH > 1) mipH /= 2;
        }

        // Transition the last mip level (was never used as a blit source)
        barrier.subresourceRange.baseMipLevel = mipLevels - 1;
        barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        device.endSingleTimeCommands(commandBuffer);
        imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    void Sampler::createTextureImageView() {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = textureImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = this->format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = mipLevels; // expose the full mip chain to shaders
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device.device(), &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create texture image view!");
        }
    }

    void Sampler::createTextureSampler(const VkSamplerAddressMode addressMode) {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = addressMode;
        samplerInfo.addressModeV = addressMode;
        samplerInfo.addressModeW = addressMode;
        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = device.properties.limits.maxSamplerAnisotropy;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = static_cast<float>(mipLevels); // was 0.0f — this was locking all textures to LOD 0

        if (vkCreateSampler(device.device(), &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create texture sampler!");
        }
    }

    void Sampler::transitionImageLayout(VkImageLayout oldLayout, VkImageLayout newLayout) {
        VkCommandBuffer commandBuffer = device.beginSingleTimeCommands();

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = textureImage;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = mipLevels; // cover all mip levels in the barrier
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        VkPipelineStageFlags sourceStage;
        VkPipelineStageFlags destinationStage;

        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        } else {
            throw std::invalid_argument("Unsupported layout transition!");
        }

        vkCmdPipelineBarrier(
            commandBuffer,
            sourceStage, destinationStage,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier
        );

        device.endSingleTimeCommands(commandBuffer);
        imageLayout = newLayout;
    }

    void Sampler::copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) const {
        VkCommandBuffer commandBuffer = device.beginSingleTimeCommands();

        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {width, height, 1};

        vkCmdCopyBufferToImage(
            commandBuffer,
            buffer,
            image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &region
        );

        device.endSingleTimeCommands(commandBuffer);
    }

    std::shared_ptr<Sampler> Sampler::create(Device &device, const void *pixels, uint32_t width, uint32_t height, VkFormat format, VkSamplerAddressMode addressMode) {
        return std::shared_ptr<Sampler>(new Sampler(device, width, height, pixels, format, addressMode));
    }

    std::shared_ptr<Sampler> Sampler::create(Device &device, const std::string &filepath, VkFormat format, VkSamplerAddressMode addressMode) {
        int32_t width, height, channels;
        void *pixels = stbi_load(filepath.c_str(), &width, &height, &channels, STBI_rgb_alpha);

        if (!pixels) {
            throw std::runtime_error("Failed to load texture image: " + filepath);
        }

        auto texture = create(device, pixels, width, height, format, addressMode);

        stbi_image_free(pixels);

        return texture;
    }

    size_t Sampler::computeHash(const void *pixels, const VkDeviceSize imageSize) {
        if (pixels == nullptr || imageSize == 0) {
            return 0;
        }

        size_t seed = 0;
        Atlas::hash(seed, imageSize);

        const auto *byteData = static_cast<const unsigned char *>(pixels);
        const auto *wordData = reinterpret_cast<const size_t *>(byteData);
        size_t numWords = imageSize / sizeof(size_t);

        for (size_t i = 0; i < numWords; ++i) {
            Atlas::hash(seed, wordData[i]);
        }

        const unsigned char *remainingBytes = byteData + (numWords * sizeof(size_t));
        size_t remaining = imageSize % sizeof(size_t);

        for (size_t i = 0; i < remaining; ++i) {
            Atlas::hash(seed, remainingBytes[i]);
        }

        return seed;
    }
}