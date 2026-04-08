#pragma once

#include "Device.hpp"
#include <string>
#include <memory>

#include "asset/Asset.hpp"

namespace Atlas {
    class Sampler final : public Asset {
    public:
        ~Sampler() override;

        Sampler(const Sampler &) = delete;
        Sampler &operator=(const Sampler &) = delete;
        Sampler(Sampler &&) = delete;
        Sampler &operator=(Sampler &&) = delete;

        [[nodiscard]] VkSampler getSampler() const { return sampler; }
        [[nodiscard]] VkImage getTexture() const { return textureImage; }
        [[nodiscard]] VkImageView getImageView() const { return imageView; }
        [[nodiscard]] VkImageLayout getImageLayout() const { return imageLayout; }

        static std::shared_ptr<Sampler> create(Device &device, const void *pixels, uint32_t width, uint32_t height, VkFormat format = VK_FORMAT_R8G8B8_SRGB, VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT);
        static std::shared_ptr<Sampler> create(Device &device, const std::string &filepath, VkFormat format = VK_FORMAT_R8G8B8_SRGB, VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT);

        static size_t computeHash(const void *pixels, VkDeviceSize imageSize);
    private:
        Sampler(Device &device, uint32_t width, uint32_t height, const void *pixels, VkFormat format, VkSamplerAddressMode addressMode);

        void createTextureImage(const void *pixels, VkDeviceSize imageSize, VkFormat format);
        void createTextureImageView();
        void createTextureSampler(VkSamplerAddressMode addressMode);

        void transitionImageLayout(VkImageLayout oldLayout, VkImageLayout newLayout);
        void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) const;
        void generateMipmaps(VkImage, VkFormat, uint32_t, uint32_t, uint32_t);

        Device &device;

        VkImage textureImage = VK_NULL_HANDLE;
        VmaAllocation textureImageMemory = VK_NULL_HANDLE;
        VkImageView imageView = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        VkImageLayout imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        uint32_t width, height;
        VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;
        uint32_t mipLevels = 1;
    };
}
