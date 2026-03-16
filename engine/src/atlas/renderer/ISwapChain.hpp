#pragma once

#include <vulkan/vulkan.h>

namespace Atlas {
    class ISwapChain {
    public:
        static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

        virtual ~ISwapChain() = default;

        ISwapChain(const ISwapChain &) = delete;
        ISwapChain &operator=(const ISwapChain &) = delete;

        virtual VkResult acquireNextImage(uint32_t *imageIndex) = 0;
        virtual VkResult submitCommandBuffers(const VkCommandBuffer *buffers, uint32_t *imageIndex) = 0;

        virtual VkFormat getImageFormat() const = 0;
        virtual VkFormat getDepthFormat() const = 0;
        virtual VkExtent2D getExtent() const = 0;
        virtual size_t imageCount() const = 0;
        virtual VkRenderPass getRenderPass() const = 0;
        virtual VkFramebuffer getFrameBuffer(int32_t index) const = 0;

        uint32_t width() const { return getExtent().width; }
        uint32_t height() const { return getExtent().height; }

        float extentAspectRatio() const {
            return static_cast<float>(getExtent().width) /
                   static_cast<float>(getExtent().height);
        }

        bool compareSwapFormats(const ISwapChain &other) const {
            return other.getImageFormat() == getImageFormat() && other.getDepthFormat() == getDepthFormat();
        }

    protected:
        ISwapChain() = default;
    };
} // namespace Atlas
