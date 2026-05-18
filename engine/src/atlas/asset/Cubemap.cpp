#include "Cubemap.hpp"

namespace Atlas {
    Cubemap::Cubemap(const std::vector<std::byte> &pixels, uint32_t width, uint32_t height, uint32_t mipLevels, VkFormat format, const std::vector<VkBufferImageCopy> &copyRegions)
        : IAsset(computeHash(pixels, width, height, mipLevels, format, copyRegions)),
          pixels_(pixels),
          width_(width),
          height_(height),
          mipLevels_(mipLevels),
          format_(format),
          copyRegions_(copyRegions) {
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

        auto hashValue = [&](const auto &value) {
            hashBytes(&value, sizeof(value));
        };

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
