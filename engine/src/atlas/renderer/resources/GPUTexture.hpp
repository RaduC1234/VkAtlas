#pragma once

#include <memory>
#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include "IGPUResource.hpp"
#include "asset/Texture.hpp"
#include "renderer/Device.hpp"

namespace Atlas {
    class GPUBuffer;

    class GPUTexture final : public IGPUResource {
    public:
        // Phase 1 — pure CPU, no command recording, no synchronization.
        // Allocates VkImage, VkImageView, VkSampler, fills staging buffer.
        // VkImageView is stable forever — safe to bind to descriptors immediately.
        // Status starts as PENDING_UPLOAD.
        GPUTexture(Device &device, const Texture &texture);
        ~GPUTexture() override;

        // Phase 2 — records upload into a graphics-compatible command buffer
        void recordUpload(VkCommandBuffer cmd) override;

        // Phase 3 — upload completion, no GPU calls, frees staging buffer
        void onUploadComplete() override;

        // Updates bindless slot from default to real VkImageView
        // Called by ResourceManager::update() after upload completion
        void updateBindlessSlot() override;

        // Stable after construction — safe to bind permanently
        VkImageView getImageView() const { return imageView_; }
        VkSampler getSampler() const { return sampler_; }
        VkImage getImage() const { return image_; }
        VkDescriptorImageInfo descriptor() const { return {sampler_, imageView_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}; }

        // Returns default view while PENDING_UPLOAD, real view when READY
        VkImageView getCurrentImageView() const {
            if (status() != Status::READY) {
                return GPUResource::default_<GPUTexture>().getImageView();
            }
            return imageView_;
        }

        // Called by render stages to register the bindless slot.
        // ResourceManager::update() will update it when READY.
        void registerBindlessSlot(VkDevice device, VkDescriptorSet set, uint32_t binding, uint32_t arrayElement);

        static std::unique_ptr<GPUTexture> createDefault(Device &device);

    private:
        void allocateImage();
        void createImageView();
        void createSampler(VkSamplerAddressMode addressMode);
        void fillStagingBuffer(const void *pixels, VkDeviceSize size);

        void recordTransition(VkCommandBuffer cmd, VkImageLayout oldLayout, VkImageLayout newLayout);
        void recordCopyBufferToImage(VkCommandBuffer cmd);
        void recordGenerateMipmaps(VkCommandBuffer cmd);

        static uint32_t bytesPerPixel(VkFormat format);
        static VkFormat resolveFormat(VkFormat requested);

        Device &device_;
        uint32_t width_ = 0;
        uint32_t height_ = 0;
        uint32_t mipLevels_ = 1;
        VkFormat format_ = VK_FORMAT_R8G8B8A8_SRGB;

        VkImage image_ = VK_NULL_HANDLE;
        VmaAllocation memory_ = VK_NULL_HANDLE;
        VkImageView imageView_ = VK_NULL_HANDLE; // real — used after READY
        VkSampler sampler_ = VK_NULL_HANDLE;
        VkImageLayout imageLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;

        // Alive from constructor to onUploadComplete()
        std::unique_ptr<GPUBuffer> stagingBuffer_;

        // Set by registerBindlessSlot(), used by updateBindlessSlot()
        struct BindlessSlot {
            VkDevice device;
            VkDescriptorSet set;
            uint32_t binding;
            uint32_t arrayElement;
        };

        std::vector<BindlessSlot> bindlessSlots_;
    };
} // namespace Atlas
