#pragma once

#include "Device.hpp"
#include <string>
#include <memory>

namespace Atlas {
    class Texture {
    public:
        Texture(Device &device, const std::string &filepath);
        ~Texture();

        Texture(const Texture &) = delete;
        Texture &operator=(const Texture &) = delete;
        Texture(Texture &&) = delete;
        Texture &operator=(Texture &&) = delete;

        VkSampler getSampler() const { return sampler; }
        VkImageView getImageView() const { return imageView; }
        VkImageLayout getImageLayout() const { return imageLayout; }

        static std::shared_ptr<Texture> createDefaultTexture(Device &device);

    private:
        // Private constructor for creating textures without loading from file
        Texture(Device &device);

        void createTextureImage(void* pixels, int width, int height);
        void createTextureImageView();
        void createTextureSampler();

        void transitionImageLayout(VkImageLayout oldLayout, VkImageLayout newLayout);
        void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);

        Device &device;

        VkImage textureImage = VK_NULL_HANDLE;
        VmaAllocation textureImageMemory = VK_NULL_HANDLE;
        VkImageView imageView = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        VkImageLayout imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        int width, height, channels;
    };
}
