#include "GPUImage.hpp"


namespace Atlas {
    GPUImage::Builder &GPUImage::Builder::setExtent(uint32_t width, uint32_t height) {
        extent_ = {width, height};
        return *this;
    }

    GPUImage::Builder &GPUImage::Builder::setFormat(VkFormat format) {
        format_ = format;
        return *this;
    }

    GPUImage::Builder &GPUImage::Builder::setUsage(VkImageUsageFlags usages) {
        usage_ = usages;
        return *this;
    }

    GPUImage::Builder &GPUImage::Builder::setMipLevels(uint32_t levels) {
        mipLevels_ = levels;
        return *this;
    }

    GPUImage::Builder &GPUImage::Builder::setArrayLayers(uint32_t layers) {
        arrayLayers_ = layers;
        return *this;
    }

    GPUImage::Builder &GPUImage::Builder::setSamples(VkSampleCountFlagBits samplers) {
        samples_ = samplers;
        return *this;
    }

    GPUImage::Builder &GPUImage::Builder::setMemoryUsage(VmaMemoryUsage memUsage) {
        memUsage_ = memUsage;
        return *this;
    }

    GPUImage::Builder &GPUImage::Builder::setInitialLayout(VkImageLayout layout) {
        initialLayout_ = layout;
        return *this;
    }

    GPUImage::Builder &GPUImage::Builder::setDebugName(std::string_view name) {
        debugName_ = name;
        return *this;
    }

    GPUImage::Builder &GPUImage::Builder::addView(VkImageAspectFlags aspect, uint32_t baseMip, uint32_t levelCount, uint32_t baseLayer, uint32_t layerCount) {
        views_.push_back({aspect, baseMip, levelCount, baseLayer, layerCount});
        return *this;
    }

    GPUImage GPUImage::Builder::build() const {
        assert(format_ != VK_FORMAT_UNDEFINED && "GPUImage::Builder: format not set");
        assert(usage_ != 0 && "GPUImage::Builder: usage not set");
        assert(!views_.empty() && "GPUImage::Builder: no views added");

        GPUImage img;
        img.device_ = &device;
        img.format_ = format_;
        img.extent_ = extent_;
        img.mipLevels_ = mipLevels_;

        VkImageCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.format = format_;
        ci.extent = {extent_.width, extent_.height, 1};
        ci.mipLevels = mipLevels_;
        ci.arrayLayers = arrayLayers_;
        ci.samples = samples_;
        ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.usage = usage_;
        ci.initialLayout = initialLayout_;
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo aci{};
        aci.usage = memUsage_;

        if (vmaCreateImage(device.allocator(), &ci, &aci, &img.image_, &img.alloc_, nullptr) != VK_SUCCESS) {
            throw std::runtime_error("GPUImage: vmaCreateImage failed" + (debugName_.empty() ? "" : " (" + debugName_ + ")"));
        }

        img.views_.reserve(views_.size());
        for (const auto &[aspect, baseMip, levelCount, baseLayer, layerCount]: views_) {
            VkImageViewCreateInfo vi{};
            vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            vi.image = img.image_;
            vi.viewType = arrayLayers_ > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
            vi.format = format_;
            vi.subresourceRange = {
                aspect,
                baseMip, levelCount,
                baseLayer, layerCount
            };

            VkImageView v = VK_NULL_HANDLE;
            if (vkCreateImageView(device.device(), &vi, nullptr, &v) != VK_SUCCESS) {
                throw std::runtime_error("GPUImage: vkCreateImageView failed" + (debugName_.empty() ? "" : " (" + debugName_ + ")"));
            }

            img.views_.push_back(v);
        }

        return img;
    }

    GPUImage::~GPUImage() {
        destroy();
    }

    GPUImage::GPUImage(GPUImage&& other) noexcept
        : device_(other.device_)
        , image_(other.image_)
        , alloc_(other.alloc_)
        , views_(std::move(other.views_))
        , format_(other.format_)
        , extent_(other.extent_)
        , mipLevels_(other.mipLevels_)
    {
        other.device_ = nullptr;
        other.image_  = VK_NULL_HANDLE;
        other.alloc_  = VK_NULL_HANDLE;
    }

    GPUImage &GPUImage::operator=(GPUImage &&other) noexcept {
        if (this != &other) {
            destroy();
            device_ = other.device_;
            image_ = other.image_;
            alloc_ = other.alloc_;
            views_ = std::move(other.views_);
            format_ = other.format_;
            extent_ = other.extent_;
            mipLevels_ = other.mipLevels_;
            other.device_ = nullptr;
            other.image_ = VK_NULL_HANDLE;
            other.alloc_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    void GPUImage::destroy() {
        if (this->image_ == VK_NULL_HANDLE) {
            return;
        }

        for (auto v : views_) {
            vkDestroyImageView(device_->device(), v, nullptr);
        }

        views_.clear();
        vmaDestroyImage(device_->allocator(), image_, alloc_);
        image_ = VK_NULL_HANDLE;
        alloc_ = VK_NULL_HANDLE;
    }
}
