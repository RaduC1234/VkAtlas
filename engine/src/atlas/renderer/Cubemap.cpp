// Cubemap.cpp
#include "Cubemap.hpp"

#include <stb_image.h>
#include <stdexcept>
#include <cstring>
#include <functional>

#include "Buffer.hpp"

namespace Atlas {

    Cubemap::~Cubemap() {
        if (sampler != VK_NULL_HANDLE) {
            vkDestroySampler(device.device(), sampler, nullptr);
        }
        if (imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(device.device(), imageView, nullptr);
        }
        if (image != VK_NULL_HANDLE) {
            vmaDestroyImage(device.allocator(), image, allocation);
        }
    }

    size_t Cubemap::computeHash(const std::array<std::string, 6> &facePaths) {
        size_t hash = 0;
        for (const auto &path : facePaths) {
            hash ^= std::hash<std::string>{}(path) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        }
        return hash;
    }

    std::shared_ptr<Cubemap> Cubemap::create(Device &device, const std::array<std::string, 6> &facePaths) {
        std::shared_ptr<Cubemap> cubemap(new Cubemap(device));

        // Load first face to get dimensions
        int texWidth, texHeight, texChannels;
        stbi_uc* firstFace = stbi_load(facePaths[0].c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

        if (!firstFace) {
            throw std::runtime_error("Failed to load cubemap face: " + facePaths[0]);
        }

        cubemap->width = static_cast<uint32_t>(texWidth);
        cubemap->height = static_cast<uint32_t>(texHeight);
        cubemap->mipLevels = 1;

        VkDeviceSize faceSize = cubemap->width * cubemap->height * 4;
        VkDeviceSize totalSize = faceSize * 6;

        // Create staging buffer using your Buffer class
        Buffer stagingBuffer(
            device,
            totalSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
        );

        stagingBuffer.map();

        // Copy first face
        stagingBuffer.uploadData(firstFace, faceSize, 0);
        stbi_image_free(firstFace);

        // Load and copy remaining 5 faces
        for (int i = 1; i < 6; i++) {
            stbi_uc* faceData = stbi_load(facePaths[i].c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

            if (!faceData) {
                throw std::runtime_error("Failed to load cubemap face: " + facePaths[i]);
            }

            if (static_cast<uint32_t>(texWidth) != cubemap->width ||
                static_cast<uint32_t>(texHeight) != cubemap->height) {
                stbi_image_free(faceData);
                throw std::runtime_error("Cubemap faces must all have the same dimensions");
            }

            stagingBuffer.uploadData(faceData, faceSize, faceSize * i);
            stbi_image_free(faceData);
        }

        stagingBuffer.unmap();

        // Create cubemap image
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = cubemap->width;
        imageInfo.extent.height = cubemap->height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = cubemap->mipLevels;
        imageInfo.arrayLayers = 6;
        imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

        if (vmaCreateImage(device.allocator(), &imageInfo, &allocInfo,&cubemap->image, &cubemap->allocation, nullptr) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create cubemap image");
        }

        cubemap->transitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        cubemap->copyBufferToImage(stagingBuffer.get());
        cubemap->transitionImageLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        cubemap->createImageView();
        cubemap->createSampler();

        return cubemap;
    }

    std::shared_ptr<Cubemap> Cubemap::create(Device &device, const std::string &hdrPath) {
        // TODO: Implement equirectangular HDR to cubemap conversion
        throw std::runtime_error("HDR cubemap loading not yet implemented");
    }

    void Cubemap::transitionImageLayout(VkImageLayout oldLayout, VkImageLayout newLayout) {
        VkCommandBuffer commandBuffer = device.beginSingleTimeCommands();

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = mipLevels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 6;

        VkPipelineStageFlags srcStage;
        VkPipelineStageFlags dstStage;

        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
            newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
                   newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        } else {
            throw std::invalid_argument("Unsupported layout transition");
        }

        vkCmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        device.endSingleTimeCommands(commandBuffer);
        imageLayout = newLayout;
    }

    void Cubemap::copyBufferToImage(VkBuffer buffer) {
        VkCommandBuffer commandBuffer = device.beginSingleTimeCommands();

        VkDeviceSize faceSize = width * height * 4;
        std::array<VkBufferImageCopy, 6> regions{};

        for (uint32_t face = 0; face < 6; face++) {
            regions[face].bufferOffset = faceSize * face;
            regions[face].bufferRowLength = 0;
            regions[face].bufferImageHeight = 0;
            regions[face].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            regions[face].imageSubresource.mipLevel = 0;
            regions[face].imageSubresource.baseArrayLayer = face;
            regions[face].imageSubresource.layerCount = 1;
            regions[face].imageOffset = {0, 0, 0};
            regions[face].imageExtent = {width, height, 1};
        }

        vkCmdCopyBufferToImage(commandBuffer, buffer, image,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              static_cast<uint32_t>(regions.size()), regions.data());

        device.endSingleTimeCommands(commandBuffer);
    }

    void Cubemap::createImageView() {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = mipLevels;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 6;

        if (vkCreateImageView(device.device(), &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create cubemap image view");
        }
    }

    void Cubemap::createSampler() {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = device.properties.limits.maxSamplerAnisotropy;
        samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = static_cast<float>(mipLevels);

        if (vkCreateSampler(device.device(), &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create cubemap sampler");
        }
    }

} // namespace Atlas