#include "Cubemap.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <array>
#include <ktx.h>
#include <stb_image.h>
#include <glm/ext/scalar_constants.hpp>

#include "core/Log.hpp"
#include "renderer/abstraction/GPUBuffer.hpp"

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
        for (const auto &path: facePaths) {
            hash ^= std::hash<std::string>{}(path) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        }
        return hash;
    }

    std::shared_ptr<Cubemap> Cubemap::create(Device &device, const std::array<std::string, 6> &facePaths) {
        std::shared_ptr<Cubemap> cubemap(new Cubemap(device));

        // Load first face to get dimensions
        int texWidth, texHeight, texChannels;
        stbi_uc *firstFace = stbi_load(facePaths[0].c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

        if (!firstFace) {
            throw std::runtime_error("Failed to load cubemap face: " + facePaths[0]);
        }

        cubemap->width = static_cast<uint32_t>(texWidth);
        cubemap->height = static_cast<uint32_t>(texHeight);
        cubemap->mipLevels = 1;

        VkDeviceSize faceSize = cubemap->width * cubemap->height * 4;
        VkDeviceSize totalSize = faceSize * 6;

        // Create staging buffer using your Buffer class
        GPUBuffer stagingBuffer(
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
            stbi_uc *faceData = stbi_load(facePaths[i].c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

            if (!faceData) {
                throw std::runtime_error("Failed to load cubemap face: " + facePaths[i]);
            }

            if (static_cast<uint32_t>(texWidth) != cubemap->width || static_cast<uint32_t>(texHeight) != cubemap->height) {
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

        if (vmaCreateImage(device.allocator(), &imageInfo, &allocInfo, &cubemap->image, &cubemap->allocation, nullptr) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create cubemap image");
        }

        cubemap->transitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        cubemap->copyBufferToImage(stagingBuffer.get(), VK_FORMAT_R8G8B8A8_SRGB);
        cubemap->transitionImageLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        cubemap->createImageView(VK_FORMAT_R8G8B8A8_SRGB);
        cubemap->createSampler();

        return cubemap;
    }

    std::shared_ptr<Cubemap> Cubemap::create(Device &device, const std::string &filePath) {
        std::shared_ptr<Cubemap> cubemap(new Cubemap(device));

        if (filePath.ends_with(".hdr")) {
            // Load HDR image
            int texWidth, texHeight, texChannels;
            float *hdrData = stbi_loadf(filePath.c_str(), &texWidth, &texHeight, &texChannels, 4);

            if (!hdrData) {
                throw std::runtime_error("Failed to load HDR image: " + filePath);
            }

            // Cubemap face size (use height of equirectangular as face size for good quality)
            uint32_t faceSize = static_cast<uint32_t>(texHeight / 2);
            cubemap->width = faceSize;
            cubemap->height = faceSize;
            cubemap->mipLevels = 1;

            VkDeviceSize faceSizeBytes = faceSize * faceSize * 4 * sizeof(float);
            VkDeviceSize totalSize = faceSizeBytes * 6;

            // Create staging buffer
            GPUBuffer stagingBuffer(
                device,
                totalSize,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VMA_MEMORY_USAGE_AUTO,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
            );

            stagingBuffer.map();

            // Direction vectors for each cubemap face. Order: +X, -X, +Y, -Y, +Z, -Z
            auto sampleEquirectangular = [&](float x, float y, float z) -> std::array<float, 4> {
                float len = std::sqrt(x * x + y * y + z * z);
                x /= len;
                y /= len;
                z /= len;

                // Convert direction to equirectangular UV
                float u = std::atan2(z, x) / (2.0f * glm::pi<float>()) + 0.5f;
                float v = std::asin(std::clamp(y, -1.0f, 1.0f)) / glm::pi<float>() + 0.5f;

                // Sample with bilinear filtering
                float fx = u * (texWidth - 1);
                float fy = (1.0f - v) * (texHeight - 1);

                int x0 = static_cast<int>(fx);
                int y0 = static_cast<int>(fy);
                int x1 = std::min(x0 + 1, texWidth - 1);
                int y1 = std::min(y0 + 1, texHeight - 1);

                float xFrac = fx - x0;
                float yFrac = fy - y0;

                std::array<float, 4> result{};
                for (int c = 0; c < 4; c++) {
                    float v00 = hdrData[(y0 * texWidth + x0) * 4 + c];
                    float v10 = hdrData[(y0 * texWidth + x1) * 4 + c];
                    float v01 = hdrData[(y1 * texWidth + x0) * 4 + c];
                    float v11 = hdrData[(y1 * texWidth + x1) * 4 + c];

                    float v0 = v00 * (1.0f - xFrac) + v10 * xFrac;
                    float v1 = v01 * (1.0f - xFrac) + v11 * xFrac;
                    result[c] = v0 * (1.0f - yFrac) + v1 * yFrac;
                }
                return result;
            };

            // Generate each cubemap face
            std::vector<float> faceData(faceSize * faceSize * 4);

            for (uint32_t face = 0; face < 6; face++) {
                for (uint32_t y = 0; y < faceSize; y++) {
                    for (uint32_t x = 0; x < faceSize; x++) {
                        // Convert pixel coordinates to [-1, 1] range
                        float u = (2.0f * (x + 0.5f) / faceSize) - 1.0f;
                        float v = (2.0f * (y + 0.5f) / faceSize) - 1.0f;

                        float dx, dy, dz;
                        switch (face) {
                            case 0: dx = 1.0f;
                                dy = -v;
                                dz = -u;
                                break; // +X
                            case 1: dx = -1.0f;
                                dy = -v;
                                dz = u;
                                break; // -X
                            case 2: dx = u;
                                dy = 1.0f;
                                dz = v;
                                break; // +Y
                            case 3: dx = u;
                                dy = -1.0f;
                                dz = -v;
                                break; // -Y
                            case 4: dx = u;
                                dy = -v;
                                dz = 1.0f;
                                break; // +Z
                            case 5: dx = -u;
                                dy = -v;
                                dz = -1.0f;
                                break; // -Z
                            default: dx = dy = dz = 0.0f;
                                break;
                        }

                        auto sample = sampleEquirectangular(dx, dy, dz);
                        uint32_t pixelIndex = (y * faceSize + x) * 4;
                        faceData[pixelIndex + 0] = sample[0];
                        faceData[pixelIndex + 1] = sample[1];
                        faceData[pixelIndex + 2] = sample[2];
                        faceData[pixelIndex + 3] = sample[3];
                    }
                }

                stagingBuffer.uploadData(faceData.data(), faceSizeBytes, faceSizeBytes * face);
            }

            stbi_image_free(hdrData);
            stagingBuffer.unmap();

            // Create cubemap image with HDR format
            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.extent.width = cubemap->width;
            imageInfo.extent.height = cubemap->height;
            imageInfo.extent.depth = 1;
            imageInfo.mipLevels = cubemap->mipLevels;
            imageInfo.arrayLayers = 6;
            imageInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
            allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

            if (vmaCreateImage(device.allocator(), &imageInfo, &allocInfo, &cubemap->image, &cubemap->allocation, nullptr) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create HDR cubemap image");
            }

            cubemap->transitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            cubemap->copyBufferToImage(stagingBuffer.get(), VK_FORMAT_R32G32B32A32_SFLOAT);
            cubemap->transitionImageLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            cubemap->createImageView(VK_FORMAT_R32G32B32A32_SFLOAT);
            cubemap->createSampler();

            return cubemap;
        }
        
        if (filePath.ends_with(".ktx2")) {
            ktxTexture2 *ktxTex = nullptr;
            if (ktxTexture2_CreateFromNamedFile(filePath.c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktxTex) != KTX_SUCCESS) {
                throw std::runtime_error("Failed to load KTX2: " + filePath);
            }

            cubemap->width = ktxTex->baseWidth;
            cubemap->height = ktxTex->baseHeight;
            cubemap->mipLevels = ktxTex->numLevels;
            VkFormat format = static_cast<VkFormat>(ktxTex->vkFormat);
            if (format == VK_FORMAT_UNDEFINED) {
                format = VK_FORMAT_R32G32B32A32_SFLOAT;
                AT_WARN("KTX2 vkFormat was UNDEFINED, defaulting to R32G32B32A32_SFLOAT");
            }

            VkDeviceSize totalSize = ktxTexture_GetDataSize(ktxTexture(ktxTex));

            GPUBuffer stagingBuffer(
                device,
                totalSize,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VMA_MEMORY_USAGE_AUTO,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
            );

            stagingBuffer.map();
            stagingBuffer.uploadData(ktxTexture_GetData(ktxTexture(ktxTex)), totalSize, 0);
            stagingBuffer.unmap();

            // Build copy regions — one per face per mip
            std::vector<VkBufferImageCopy> regions;
            for (uint32_t mip = 0; mip < cubemap->mipLevels; mip++) {
                for (uint32_t face = 0; face < 6; face++) {
                    ktx_size_t offset;
                    ktxTexture_GetImageOffset(ktxTexture(ktxTex), mip, 0, face, &offset);

                    VkBufferImageCopy region{};
                    region.bufferOffset = offset;
                    region.bufferRowLength = 0;
                    region.bufferImageHeight = 0;
                    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    region.imageSubresource.mipLevel = mip;
                    region.imageSubresource.baseArrayLayer = face;
                    region.imageSubresource.layerCount = 1;
                    region.imageOffset = {0, 0, 0};
                    region.imageExtent = {
                        std::max(1u, cubemap->width >> mip),
                        std::max(1u, cubemap->height >> mip),
                        1
                    };
                    regions.push_back(region);
                }
            }

            // Create image
            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.extent = {cubemap->width, cubemap->height, 1};
            imageInfo.mipLevels = cubemap->mipLevels;
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

            if (vmaCreateImage(device.allocator(), &imageInfo, &allocInfo,
                               &cubemap->image, &cubemap->allocation, nullptr) != VK_SUCCESS) {
                ktxTexture_Destroy(ktxTexture(ktxTex));
                throw std::runtime_error("Failed to create KTX2 cubemap image");
            }

            // Upload
            cubemap->transitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            GPUBuffer::copyToImage(device, stagingBuffer.get(), cubemap->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, regions);
            cubemap->transitionImageLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            cubemap->createImageView(format);
            cubemap->createSampler();

            ktxTexture_Destroy(ktxTexture(ktxTex));
            return cubemap;
        }
        
        AT_ERROR("Unsupported cubemap file type: {}", filePath);
        return nullptr;
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
            throw std::invalid_argument("Unsupported layout transition");
        }

        vkCmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        device.endSingleTimeCommands(commandBuffer);
        imageLayout = newLayout;
    }

    void Cubemap::copyBufferToImage(VkBuffer buffer, VkFormat format) {
        VkCommandBuffer commandBuffer = device.beginSingleTimeCommands();

        // Calculate bytes per pixel based on format
        VkDeviceSize bytesPerPixel = (format == VK_FORMAT_R32G32B32A32_SFLOAT) ? 16 : 4;
        VkDeviceSize faceSize = width * height * bytesPerPixel;
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

    void Cubemap::createImageView(VkFormat format) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        viewInfo.format = format;
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
