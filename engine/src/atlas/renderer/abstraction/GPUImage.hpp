#pragma once

#include "renderer/Device.hpp"

namespace Atlas {
    class GPUImage {
    public:
        class Builder {
        public:
            explicit Builder(Device &device) : device(device) {
            }

            Builder &setExtent(uint32_t width, uint32_t height);
            Builder &setFormat(VkFormat format);
            Builder &setUsage(VkImageUsageFlags usages);
            Builder &setMipLevels(uint32_t levels);
            Builder &setArrayLayers(uint32_t layers);
            Builder &setSamples(VkSampleCountFlagBits samplers);
            Builder &setMemoryUsage(VmaMemoryUsage memUsage);
            Builder &setInitialLayout(VkImageLayout layout);
            Builder &setDebugName(std::string_view name);

            // Each call appends one VkImageView to the image.
            // view(0) is the first added, view(1) the second, etc.
            Builder &addView(VkImageAspectFlags aspect, uint32_t baseMip = 0, uint32_t levelCount = 1, uint32_t baseLayer = 0, uint32_t layerCount = 1);

            [[nodiscard]] GPUImage build() const;

        private:
            Device &device;
            VkExtent2D extent_ = {};
            VkFormat format_ = VK_FORMAT_UNDEFINED;
            VkImageUsageFlags usage_ = 0;
            uint32_t mipLevels_ = 1;
            uint32_t arrayLayers_ = 1;
            VkSampleCountFlagBits samples_ = VK_SAMPLE_COUNT_1_BIT;
            VmaMemoryUsage memUsage_ = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            VkImageLayout initialLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
            std::string debugName_;

            struct ViewDesc {
                VkImageAspectFlags aspect;
                uint32_t baseMip, levelCount, baseLayer, layerCount;
            };

            std::vector<ViewDesc> views_;
        };

        GPUImage() = default;
        ~GPUImage();

        GPUImage(const GPUImage &) = delete;
        GPUImage &operator=(const GPUImage &) = delete;
        GPUImage(GPUImage &&) noexcept;
        GPUImage &operator=(GPUImage &&) noexcept;

        VkImage image() const { return image_; }
        VkImageView view(uint32_t i) const { return views_.at(i); } // invalid vector subscript
        uint32_t viewCount() const { return static_cast<uint32_t>(views_.size()); }
        VkFormat format() const { return format_; }
        VkExtent2D extent() const { return extent_; }
        uint32_t mipLevels() const { return mipLevels_; }
        bool valid() const { return image_ != VK_NULL_HANDLE; }

        void destroy(); // explicit early release if needed

    private:
        friend class Builder;

        Device *device_ = nullptr;
        VkImage image_ = VK_NULL_HANDLE;
        VmaAllocation alloc_ = VK_NULL_HANDLE;
        std::vector<VkImageView> views_;
        VkFormat format_ = VK_FORMAT_UNDEFINED;
        VkExtent2D extent_ = {};
        uint32_t mipLevels_ = 1;
    };
} // namespace Atlas
