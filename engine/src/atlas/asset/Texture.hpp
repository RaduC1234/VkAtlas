#pragma once
#include <cstddef>
#include <vector>

#include "IAsset.hpp"
#include "renderer/Device.hpp"

namespace Atlas {
    class Texture : public IAsset {
    public:
        Texture(const std::vector<std::byte> &pixels, uint32_t width, uint32_t height, VkFormat format, VkSamplerAddressMode addressMode);

        const std::vector<std::byte> &pixels() const { return pixels_; }
        uint32_t width() const { return width_; }
        uint32_t height() const { return height_; }
        VkFormat format() const { return format_; }
        VkSamplerAddressMode addressMode() const { return addressMode_; }

    private:
        static uint64_t computeHash(const std::vector<std::byte> &pixels, uint32_t width, uint32_t height, VkFormat format, VkSamplerAddressMode addressMode);

        std::vector<std::byte> pixels_;
        uint32_t width_{0};
        uint32_t height_{0};
        VkFormat format_ = VK_FORMAT_R8G8B8_SRGB;
        VkSamplerAddressMode addressMode_ = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    };
}
