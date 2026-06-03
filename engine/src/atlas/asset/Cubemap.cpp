#include "Cubemap.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <stdexcept>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <stb_image.h>
#include <ktx.h>

#include "AssetManager.hpp"

namespace Atlas {
    Cubemap::Cubemap(const std::vector<std::byte> &pixels, uint32_t width, uint32_t height, uint32_t mipLevels, VkFormat format, const std::vector<VkBufferImageCopy> &copyRegions)
        : IAsset(computeHash(pixels, width, height, mipLevels, format, copyRegions))
          , pixels_(pixels)
          , width_(width)
          , height_(height)
          , mipLevels_(mipLevels)
          , format_(format)
          , copyRegions_(copyRegions) {
    }

    std::shared_ptr<Cubemap> Cubemap::fromFaces(const std::array<std::vector<std::byte>, 6> &faceData, VkFormat format) {
        uint32_t faceW = 0, faceH = 0;
        const uint32_t bytesPerPixel = 4; // always RGBA8
        std::vector<std::byte> combined;
        std::vector<VkBufferImageCopy> regions;
        regions.reserve(6);

        for (uint32_t face = 0; face < 6; ++face) {
            int w = 0, h = 0, channels = 0;
            unsigned char *decoded = stbi_load_from_memory(
                reinterpret_cast<const stbi_uc *>(faceData[face].data()),
                static_cast<int>(faceData[face].size()),
                &w, &h, &channels, STBI_rgb_alpha
            );

            if (!decoded) {
                throw std::runtime_error("Cubemap::fromFaces — stb decode failed for face " + std::to_string(face) + ": " + stbi_failure_reason());
            }
            if (face == 0) {
                faceW = static_cast<uint32_t>(w);
                faceH = static_cast<uint32_t>(h);
                combined.reserve(static_cast<size_t>(faceW) * faceH * bytesPerPixel * 6);
            } else if (static_cast<uint32_t>(w) != faceW || static_cast<uint32_t>(h) != faceH) {
                stbi_image_free(decoded);
                throw std::runtime_error("Cubemap::fromFaces — face dimensions mismatch");
            }

            const size_t faceBytes = static_cast<size_t>(faceW) * faceH * bytesPerPixel;
            const VkDeviceSize offset = combined.size();

            combined.resize(combined.size() + faceBytes);
            std::memcpy(combined.data() + offset,
                        reinterpret_cast<const std::byte *>(decoded), faceBytes);
            stbi_image_free(decoded);

            VkBufferImageCopy region{};
            region.bufferOffset = offset;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = face,
                .layerCount = 1,
            };
            region.imageOffset = {0, 0, 0};
            region.imageExtent = {faceW, faceH, 1};
            regions.push_back(region);
        }

        return std::make_shared<Cubemap>(combined, faceW, faceH, 1, format, regions);
    }

    std::shared_ptr<Cubemap> Cubemap::fromFile(const std::string &path, uint32_t equirectFaceSize) {
        std::string ext = std::filesystem::path(path).extension().string();
        std::ranges::transform(ext, ext.begin(), ::tolower);

        if (ext == ".ktx2" || ext == ".ktx") {
            // ktx has its own file API — no need to read into memory first
            return fromKtx2(path);
        }

        return fromEquirectangular(AssetManager::loadFileAs<std::byte>(path), equirectFaceSize);
    }

    std::shared_ptr<Cubemap> Cubemap::fromEquirectangular(const std::vector<std::byte> &imageData, uint32_t faceSize, VkFormat format) {
        const stbi_uc *raw = reinterpret_cast<const stbi_uc *>(imageData.data());
        const int len = static_cast<int32_t>(imageData.size());

        const bool isHDR = stbi_is_hdr_from_memory(raw, len);

        if (format == VK_FORMAT_UNDEFINED) {
            format = isHDR ? VK_FORMAT_R32G32B32A32_SFLOAT : VK_FORMAT_R8G8B8A8_SRGB;
        }

        int srcW = 0, srcH = 0, channels = 0;

        float *srcF = nullptr; // HDR path
        unsigned char *srcU = nullptr; // LDR path

        if (isHDR) {
            srcF = stbi_loadf_from_memory(raw, len, &srcW, &srcH, &channels, STBI_rgb_alpha);
            if (!srcF) {
                throw std::runtime_error("Cubemap::fromEquirectangular — HDR decode failed: " + std::string(stbi_failure_reason()));
            }
        } else {
            srcU = stbi_load_from_memory(raw, len, &srcW, &srcH, &channels, STBI_rgb_alpha);
            if (!srcU) {
                throw std::runtime_error("Cubemap::fromEquirectangular — LDR decode failed: " + std::string(stbi_failure_reason()));
            }
        }

        // ---- Allocate output ------------------------------------------------
        const uint32_t bytesPerPixel = isHDR ? 16u : 4u; // RGBA32F or RGBA8
        const size_t faceBytes = static_cast<size_t>(faceSize) * faceSize * bytesPerPixel;
        std::vector<std::byte> combined(faceBytes * 6);
        std::vector<VkBufferImageCopy> regions;
        regions.reserve(6);

        // ---- Face bases: +X -X +Y -Y +Z -Z ----------------------------------
        struct FaceBasis {
            glm::vec3 right, up, forward;
        };
        const std::array<FaceBasis, 6> bases = {
            {
                {{0, 0, -1}, {0, -1, 0}, {1, 0, 0}}, // +X
                {{0, 0, 1}, {0, -1, 0}, {-1, 0, 0}}, // -X
                {{1, 0, 0}, {0, 0, 1}, {0, 1, 0}}, // +Y
                {{1, 0, 0}, {0, 0, -1}, {0, -1, 0}}, // -Y
                {{1, 0, 0}, {0, -1, 0}, {0, 0, 1}}, // +Z
                {{-1, 0, 0}, {0, -1, 0}, {0, 0, -1}}, // -Z
            }
        };

        // ---- Bilinear sampler -----------------------------------------------
        // Returns 4 floats regardless of source type; caller writes them as
        // float or truncates to uint8 depending on isHDR.
        auto sampleEquirect = [&](glm::vec3 dir) -> std::array<float, 4> {
            dir = glm::normalize(dir);
            const float phi = std::atan2(dir.z, dir.x);
            const float theta = std::asin(glm::clamp(dir.y, -1.0f, 1.0f));

            const float u = (phi / (2.0f * glm::pi<float>()) + 0.5f) * static_cast<float>(srcW - 1);
            const float v = (0.5f - theta / glm::pi<float>()) * static_cast<float>(srcH - 1);

            const int x0 = static_cast<int>(u), y0 = static_cast<int>(v);
            const int x1 = std::min(x0 + 1, srcW - 1), y1 = std::min(y0 + 1, srcH - 1);
            const float fx = u - static_cast<float>(x0);
            const float fy = v - static_cast<float>(y0);

            std::array<float, 4> out{};
            for (int c = 0; c < 4; ++c) {
                float s00, s10, s01, s11;
                if (isHDR) {
                    s00 = srcF[(y0 * srcW + x0) * 4 + c];
                    s10 = srcF[(y0 * srcW + x1) * 4 + c];
                    s01 = srcF[(y1 * srcW + x0) * 4 + c];
                    s11 = srcF[(y1 * srcW + x1) * 4 + c];
                } else {
                    s00 = static_cast<float>(srcU[(y0 * srcW + x0) * 4 + c]);
                    s10 = static_cast<float>(srcU[(y0 * srcW + x1) * 4 + c]);
                    s01 = static_cast<float>(srcU[(y1 * srcW + x0) * 4 + c]);
                    s11 = static_cast<float>(srcU[(y1 * srcW + x1) * 4 + c]);
                }
                out[c] = s00 * (1 - fx) * (1 - fy) + s10 * fx * (1 - fy)
                         + s01 * (1 - fx) * fy + s11 * fx * fy;
            }
            return out;
        };

        // ---- Rasterise faces ------------------------------------------------
        for (uint32_t face = 0; face < 6; ++face) {
            const size_t faceOffset = faceBytes * face;
            auto *dst = reinterpret_cast<uint8_t *>(combined.data()) + faceOffset;
            const FaceBasis &b = bases[face];

            for (uint32_t y = 0; y < faceSize; ++y) {
                for (uint32_t x = 0; x < faceSize; ++x) {
                    const float nx = (static_cast<float>(x) + 0.5f) / static_cast<float>(faceSize) * 2.0f - 1.0f;
                    const float ny = (static_cast<float>(y) + 0.5f) / static_cast<float>(faceSize) * 2.0f - 1.0f;
                    const glm::vec3 dir = glm::normalize(b.forward + nx * b.right + ny * b.up);
                    const auto pixel = sampleEquirect(dir);

                    const size_t pixelOffset = (static_cast<size_t>(y) * faceSize + x) * bytesPerPixel;

                    if (isHDR) {
                        std::memcpy(dst + pixelOffset, pixel.data(), 16);
                    } else {
                        uint8_t ldr[4];
                        for (int c = 0; c < 4; ++c)
                            ldr[c] = static_cast<uint8_t>(glm::clamp(pixel[c], 0.0f, 255.0f));
                        std::memcpy(dst + pixelOffset, ldr, 4);
                    }
                }
            }

            VkBufferImageCopy region{};
            region.bufferOffset = static_cast<VkDeviceSize>(faceOffset);
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = face,
                .layerCount = 1,
            };
            region.imageOffset = {0, 0, 0};
            region.imageExtent = {faceSize, faceSize, 1};
            regions.push_back(region);
        }

        if (isHDR) stbi_image_free(srcF);
        else stbi_image_free(srcU);

        return std::make_shared<Cubemap>(combined, faceSize, faceSize, 1, format, regions);
    }

    std::shared_ptr<Cubemap> Cubemap::fromKtx2(const std::vector<std::byte> &fileData) {
        ktxTexture2 *ktx = nullptr;
        const ktxResult result = ktxTexture2_CreateFromMemory(
            reinterpret_cast<const ktx_uint8_t *>(fileData.data()),
            fileData.size(),
            KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
            &ktx
        );

        if (result != KTX_SUCCESS) {
            throw std::runtime_error("Cubemap::fromKtx2 — ktxTexture2_CreateFromMemory failed");
        }

        if (ktx->numFaces != 6) {
            ktxTexture_Destroy(ktxTexture(ktx));
            throw std::runtime_error("Cubemap::fromKtx2 — KTX2 file does not contain a cubemap (numFaces != 6)");
        }

        if (ktxTexture2_NeedsTranscoding(ktx)) {
            if (ktxTexture2_TranscodeBasis(ktx, KTX_TTF_BC7_RGBA, 0) != KTX_SUCCESS) {
                ktxTexture_Destroy(ktxTexture(ktx));
                throw std::runtime_error("Cubemap::fromKtx2 — BasisU transcode failed");
            }
        }

        const uint32_t width = ktx->baseWidth;
        const uint32_t height = ktx->baseHeight;
        const uint32_t mipLevels = ktx->numLevels;
        const auto format = static_cast<VkFormat>(ktx->vkFormat);

        // Copy all pixel data into a single contiguous buffer
        const size_t totalBytes = ktxTexture_GetDataSize(ktxTexture(ktx));
        std::vector<std::byte> pixels(totalBytes);
        std::memcpy(pixels.data(), ktxTexture_GetData(ktxTexture(ktx)), totalBytes);

        // Build one copy region per (mip level, face)
        std::vector<VkBufferImageCopy> regions;
        regions.reserve(mipLevels * 6);

        for (uint32_t mip = 0; mip < mipLevels; ++mip) {
            for (uint32_t face = 0; face < 6; ++face) {
                ktx_size_t offset = 0;
                ktxTexture_GetImageOffset(ktxTexture(ktx), mip, 0, face, &offset);

                const uint32_t mipW = std::max(1u, width >> mip);
                const uint32_t mipH = std::max(1u, height >> mip);

                VkBufferImageCopy region{};
                region.bufferOffset = static_cast<VkDeviceSize>(offset);
                region.bufferRowLength = 0;
                region.bufferImageHeight = 0;
                region.imageSubresource = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = mip,
                    .baseArrayLayer = face,
                    .layerCount = 1,
                };
                region.imageOffset = {0, 0, 0};
                region.imageExtent = {mipW, mipH, 1};
                regions.push_back(region);
            }
        }

        ktxTexture_Destroy(ktxTexture(ktx));
        return std::make_shared<Cubemap>(pixels, width, height, mipLevels, format, regions);
    }

    std::shared_ptr<Cubemap> Cubemap::fromKtx2(const std::string &path) {
        return fromKtx2(AssetManager::loadFileAs<std::byte>(path));
    }

    uint64_t Cubemap::computeHash(const std::vector<std::byte> &pixels, uint32_t width, uint32_t height, uint32_t mipLevels, VkFormat format, const std::vector<VkBufferImageCopy> &copyRegions) {
        constexpr uint64_t FNV_OFFSET_BASIS = 14695981039346656037ull;
        constexpr uint64_t FNV_PRIME = 1099511628211ull;

        uint64_t hash = FNV_OFFSET_BASIS;

        auto hashBytes = [&](const void *data, size_t size) {
            const auto *bytes = static_cast<const std::byte *>(data);
            for (size_t i = 0; i < size; ++i) {
                hash ^= std::to_integer<uint8_t>(bytes[i]);
                hash *= FNV_PRIME;
            }
        };

        auto hashValue = [&](const auto &value) { hashBytes(&value, sizeof(value)); };

        hashValue(width);
        hashValue(height);
        hashValue(mipLevels);
        hashValue(format);

        if (!copyRegions.empty())
            hashBytes(copyRegions.data(), copyRegions.size() * sizeof(VkBufferImageCopy));

        if (!pixels.empty())
            hashBytes(pixels.data(), pixels.size());

        return hash;
    }
}
