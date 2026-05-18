#include "Texture.hpp"

namespace Atlas {
    Texture::Texture(const std::vector<std::byte> &pixels, const uint32_t width, const uint32_t height, const VkFormat format, const VkSamplerAddressMode addressMode)
        : IAsset(computeHash(pixels, width, height, format, addressMode)), pixels_(pixels), width_(width), height_(height), format_(format), addressMode_(addressMode) {
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
