#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>

#include "IAsset.hpp"

namespace Atlas {
    class Cubemap : public IAsset {
    public:
        Cubemap(const std::vector<std::byte> &pixels, uint32_t width, uint32_t height, uint32_t mipLevels, VkFormat format, const std::vector<VkBufferImageCopy> &copyRegions);

        const std::vector<std::byte> &pixels() const { return pixels_; }
        uint32_t width() const { return width_; }
        uint32_t height() const { return height_; }
        uint32_t mipLevels() const { return mipLevels_; }
        VkFormat format() const { return format_; }
        const std::vector<VkBufferImageCopy> &copyRegions() const { return copyRegions_; }

    private:
        static uint64_t computeHash(const std::vector<std::byte> &pixels, uint32_t width, uint32_t height, uint32_t mipLevels, VkFormat format, const std::vector<VkBufferImageCopy> &copyRegions);

        std::vector<std::byte> pixels_;
        uint32_t width_{0};
        uint32_t height_{0};
        uint32_t mipLevels_{1};
        VkFormat format_{VK_FORMAT_R8G8B8A8_SRGB};
        std::vector<VkBufferImageCopy> copyRegions_;
    };
}
