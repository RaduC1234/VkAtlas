#pragma once
#include "Device.hpp"
#include "asset/Asset.hpp"

namespace Atlas {
    class Cubemap : public Asset {
    public:
        ~Cubemap() override;

        Cubemap(const Cubemap &) = delete;
        Cubemap &operator=(const Cubemap &) = delete;

        VkImageView getImageView() const { return imageView; }
        VkSampler getSampler() const { return sampler; }
        VkImageLayout getImageLayout() const { return imageLayout; }

        VkDescriptorImageInfo descriptorInfo() const {
            return VkDescriptorImageInfo{sampler, imageView, imageLayout};
        }

        static size_t computeHash(const std::array<std::string, 6> &facePaths);

        static std::shared_ptr<Cubemap> create(Device &device, const std::array<std::string, 6> &facePaths);
        static std::shared_ptr<Cubemap> create(Device &device, const std::string &hdrPath);

    private:
        Cubemap(Device &device) : device(device) {}

        void transitionImageLayout(VkImageLayout oldLayout, VkImageLayout newLayout);
        void copyBufferToImage(VkBuffer buffer);
        void createImageView();
        void createSampler();

        Device &device;

        VkImage image{VK_NULL_HANDLE};
        VmaAllocation allocation{VK_NULL_HANDLE};
        VkImageView imageView{VK_NULL_HANDLE};
        VkSampler sampler{VK_NULL_HANDLE};
        VkImageLayout imageLayout{VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

        uint32_t width{0};
        uint32_t height{0};
        uint32_t mipLevels{0};
    };
}
