#pragma once

#include <memory>
#include <optional>
#include <vector>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include "IGPUResource.hpp"
#include "asset/Cubemap.hpp"
#include "renderer/Device.hpp"
#include "renderer/abstraction/GPUBuffer.hpp"

namespace Atlas {
    class GPUCubemap final : public IGPUResource {
    public:
        GPUCubemap(Device &device, const Cubemap &cubemap);
        ~GPUCubemap() override;

        GPUCubemap(const GPUCubemap &) = delete;
        GPUCubemap &operator=(const GPUCubemap &) = delete;

        void recordTransfer(VkCommandBuffer cmd) override;
        void onTransferComplete() override;
        void recordOwnershipAcquire(VkCommandBuffer cmd) override;
        void updateBindlessSlot() override;

        VkImageView getImageView() const { return imageView_; }
        VkSampler getSampler() const { return sampler_; }
        VkImageLayout getImageLayout() const { return imageLayout_; }

        VkDescriptorImageInfo descriptor() const {
            return {sampler_, imageView_, imageLayout_};
        }

        VkImageView getCurrentImageView() const {
            if (status() != Status::READY) {
                return GPUResource::default_<GPUCubemap>().getImageView();
            }
            return imageView_;
        }

        void registerBindlessSlot(VkDevice device, VkDescriptorSet set, uint32_t binding, uint32_t arrayElement);

        static std::unique_ptr<GPUCubemap> createDefault(Device &device);

    private:
        void allocateImage(VkFormat format);
        void createImageView(VkFormat format);
        void createSampler();
        void fillStagingBuffer(const void *pixels, VkDeviceSize size);

        void recordTransition(VkCommandBuffer cmd, VkImageLayout oldLayout, VkImageLayout newLayout);
        void recordCopyBufferToImage(VkCommandBuffer cmd);
        void recordOwnershipRelease(VkCommandBuffer cmd);

        Device &device_;

        uint32_t width_{0};
        uint32_t height_{0};
        uint32_t mipLevels_{1};
        VkFormat format_{VK_FORMAT_R8G8B8A8_SRGB};

        VkImage image_{VK_NULL_HANDLE};
        VmaAllocation allocation_{VK_NULL_HANDLE};
        VkImageView imageView_{VK_NULL_HANDLE};
        VkSampler sampler_{VK_NULL_HANDLE};
        VkImageLayout imageLayout_{VK_IMAGE_LAYOUT_UNDEFINED};

        std::vector<VkBufferImageCopy> copyRegions_;
        std::unique_ptr<GPUBuffer> stagingBuffer_;

        struct BindlessSlot {
            VkDevice device;
            VkDescriptorSet set;
            uint32_t binding;
            uint32_t arrayElement;
        };

        std::optional<BindlessSlot> bindlessSlot_;
    };
} // namespace Atlas
