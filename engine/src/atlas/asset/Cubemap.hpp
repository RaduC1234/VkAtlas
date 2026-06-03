#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
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

        static std::shared_ptr<Cubemap> fromFile(const std::string &path, uint32_t equirectFaceSize = 512);
        static std::shared_ptr<Cubemap> fromFaces(const std::array<std::vector<std::byte>, 6> &faceData,VkFormat format = VK_FORMAT_R8G8B8A8_SRGB);
        /* Single equirectangular image (LDR or HDR) — converted to 6-face layout on the CPU.
         * Format is auto-detected from the image data; pass VK_FORMAT_UNDEFINED to let it decide.
         */
        static std::shared_ptr<Cubemap> fromEquirectangular(const std::vector<std::byte> &imageData,uint32_t faceSize = 512,VkFormat format = VK_FORMAT_UNDEFINED);
        static std::shared_ptr<Cubemap> fromKtx2(const std::vector<std::byte> &fileData);
        static std::shared_ptr<Cubemap> fromKtx2(const std::string &path);

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
