#pragma once

#include "Device.hpp"

namespace Atlas {
    class Framebuffer {
    public:
        enum class AttachmentType { Color, Depth };

        struct AttachmentSpecification {
            VkFormat format;
            AttachmentType type;
        };

        Framebuffer(Device &device, VkExtent2D extent, std::vector<AttachmentSpecification> attachments);
        ~Framebuffer();

        Framebuffer(const Framebuffer &) = delete;
        Framebuffer &operator=(const Framebuffer &) = delete;

        VkRenderPass getRenderPass() const { return renderPass; }
        VkFramebuffer getFramebuffer() const { return framebuffer; }
        VkImageView getAttachmentView(uint32_t index) const { return attachments[index].imageView; }
        VkExtent2D getExtent() const { return extent; }

        VkDescriptorImageInfo getDescriptorInfo(uint32_t index, VkSampler sampler) const;

    private:
        struct Attachment {
            VkImage image = VK_NULL_HANDLE;
            VmaAllocation allocation = VK_NULL_HANDLE;
            VkImageView imageView = VK_NULL_HANDLE;
            AttachmentSpecification spec;
        };

        void createAttachments();
        void createRenderPass();
        void createFramebuffer();

        Device &device;
        VkExtent2D extent;
        std::vector<AttachmentSpecification> attachmentsSpecs;
        std::vector<Attachment> attachments;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
    };
}
