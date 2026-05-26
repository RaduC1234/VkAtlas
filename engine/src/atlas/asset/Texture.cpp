#include "Texture.hpp"

#include <cstring>
#include <stdexcept>

#include <ktx.h>

#include "AssetManager.hpp"
#include "stb_image.h"

namespace Atlas {
    Texture::Texture(const std::vector<std::byte> &pixels, const uint32_t width, const uint32_t height, const VkFormat format, const VkSamplerAddressMode addressMode)
        : IAsset(computeHash(pixels, width, height, format, addressMode)), pixels_(pixels), width_(width), height_(height), format_(format), addressMode_(addressMode) {
    }

    std::shared_ptr<Texture> Texture::fromFile(const std::string &path, VkSamplerAddressMode addressMode) {
        const auto raw = AssetManager::loadFileAs<std::byte>(path);

        std::string ext = std::filesystem::path(path).extension().string();
        std::ranges::transform(ext, ext.begin(), ::tolower);

        if (ext == ".ktx2" || ext == ".ktx") {
            return fromKtx2(raw, addressMode);
        }

        if (ext == ".hdr") {
            int w, h, channels;
            float *pixels = stbi_loadf_from_memory(
                reinterpret_cast<const stbi_uc *>(raw.data()),
                static_cast<int>(raw.size()),
                &w, &h, &channels, STBI_rgb_alpha
            );

            if (!pixels) {
                throw std::runtime_error("Texture::fromFile — HDR decode failed: " + path);
            }

            const auto span = std::span(pixels, w * h * 4);
            const auto bytes = std::vector(std::as_bytes(span).begin(), std::as_bytes(span).end());
            auto tex = fromHDR(bytes, static_cast<uint32_t>(w), static_cast<uint32_t>(h), addressMode);
            stbi_image_free(pixels);
            return tex;
        }

        if (ext == ".lut.bin") {
            constexpr uint32_t w = 64, h = 64;
            constexpr size_t expected = w * h * 4 * sizeof(float);
            if (raw.size() != expected) {
                throw std::runtime_error("Texture::fromFile — unexpected .bin size: " + path);
            }
            return fromHDR(
                {raw.begin(), raw.end()},
                w, h, addressMode
            );
        }

        // PNG, JPG, BMP, TGA, etc.
        int w, h, channels;
        unsigned char *pixels = stbi_load_from_memory(
            reinterpret_cast<const stbi_uc *>(raw.data()),
            static_cast<int>(raw.size()),
            &w, &h, &channels, STBI_rgb_alpha
        );

        if (!pixels) {
            throw std::runtime_error("Texture::fromFile — decode failed: " + path);
        }

        const auto span = std::span(pixels, w * h * 4);
        const auto bytes = std::vector(std::as_bytes(span).begin(), std::as_bytes(span).end());
        auto tex = fromRGBA8(bytes, static_cast<uint32_t>(w), static_cast<uint32_t>(h), addressMode);
        stbi_image_free(pixels);
        return tex;
    }

    std::shared_ptr<Texture> Texture::fromRGBA8(const std::vector<std::byte> &data, uint32_t width, uint32_t height, VkSamplerAddressMode addressMode) {
        return std::make_shared<Texture>(data, width, height, VK_FORMAT_R8G8B8A8_SRGB, addressMode);
    }

    std::shared_ptr<Texture> Texture::fromHDR(const std::vector<std::byte> &data, uint32_t width, uint32_t height, VkSamplerAddressMode addressMode) {
        return std::make_shared<Texture>(data, width, height, VK_FORMAT_R32G32B32A32_SFLOAT, addressMode);
    }

    std::shared_ptr<Texture> Texture::fromKtx2(const std::vector<std::byte> &fileData, VkSamplerAddressMode addressMode) {
        ktxTexture2 *ktx = nullptr;
        const ktxResult result = ktxTexture2_CreateFromMemory(
            reinterpret_cast<const ktx_uint8_t *>(fileData.data()),
            fileData.size(),
            KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
            &ktx
        );

        if (result != KTX_SUCCESS) {
            throw std::runtime_error("Failed to load KTX2 texture");
        }

        if (ktxTexture2_NeedsTranscoding(ktx)) {
            ktxTexture2_TranscodeBasis(ktx, KTX_TTF_BC7_RGBA, 0);
        }

        const uint32_t width = ktx->baseWidth;
        const uint32_t height = ktx->baseHeight;
        const auto format = static_cast<VkFormat>(ktx->vkFormat);

        ktx_uint8_t *imageData = ktxTexture_GetData(ktxTexture(ktx));
        const ktx_size_t imageSize = ktxTexture_GetDataSize(ktxTexture(ktx));

        std::vector<std::byte> bytes(imageSize);
        std::memcpy(bytes.data(), imageData, imageSize);

        ktxTexture_Destroy(ktxTexture(ktx));

        return std::make_shared<Texture>(bytes, width, height, format, addressMode);
    }

    std::shared_ptr<Texture> Texture::default_() {
        constexpr unsigned char px[4] = {255, 255, 255, 255};
        const auto *begin = reinterpret_cast<const std::byte *>(px);
        return fromRGBA8(std::vector<std::byte>(begin, begin + 4), 1, 1);
    }


    uint64_t Texture::computeHash(const std::vector<std::byte> &pixels, uint32_t width, uint32_t height, VkFormat format, VkSamplerAddressMode addressMode) {
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

        auto hashValue = [&](const auto &value) {
            hashBytes(&value, sizeof(value));
        };

        hashValue(width);
        hashValue(height);
        hashValue(format);
        hashValue(addressMode);

        if (!pixels.empty())
            hashBytes(pixels.data(), pixels.size());

        return hash;
    }
}
