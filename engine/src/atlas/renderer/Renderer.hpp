#pragma once

#include "Device.hpp"

#include "core/Window.hpp"
#include "swapchain/SwapChain.hpp"

#include <cassert>
#include <memory>
#include <vector>

namespace Atlas {
    struct FrameContext {
        VkCommandBuffer graphicsCommandBuffer;
        VkCommandBuffer computeCommandBuffer;
        uint32_t index;
    };

    class Renderer {
    public:
        enum class SceneOutputTarget {
            Swapchain,
            Texture
        };

        enum class OverlayLoadOp {
            Load,
            Clear
        };

        struct SceneOutputImage {
            VkImageView imageView = VK_NULL_HANDLE;
            VkImageLayout imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            VkExtent2D extent{};

            [[nodiscard]] bool valid() const { return imageView != VK_NULL_HANDLE; }
        };

        struct Settings {
            Window::Settings windowSettings;
            bool enableRaytracing = false;
            SceneOutputTarget sceneOutputTarget = SceneOutputTarget::Swapchain;
        };

        Renderer(const Settings &settings);
        ~Renderer();

        Renderer(const Renderer &) = delete;
        Renderer &operator=(const Renderer &) = delete;

        Window &window() const { return *window_; }
        Device &device() const { return *device_; }

        VkRenderPass getSwapChainRenderPass() const { return swapChain_->getRenderPass(); }
        VkRenderPass getOverlayRenderPass(OverlayLoadOp loadOp = OverlayLoadOp::Load) const {
            return loadOp == OverlayLoadOp::Clear
                ? swapChain_->getOverlayClearRenderPass()
                : swapChain_->getOverlayRenderPass();
        }
        float getAspectRatio() const;
        size_t getImageCount() const { return swapChain_->imageCount(); }
        VkImage getCurrentSwapchainImage() const { return swapChain_->getImage(currentImageIndex); }
        VkExtent2D getSwapchainExtent() const { return swapChain_->getSwapChainExtent(); }
        const SceneOutputImage &getSceneOutputImage() const { return sceneOutputImage; }
        VkCommandBuffer currentGraphicsCommandBuffer() const;

        FrameContext beginFrame();
        void endFrame();

        void beginSwapChainRenderPass(VkCommandBuffer);
        void endSwapChainRenderPass(VkCommandBuffer) const;
        void beginOverlayRenderPass(VkCommandBuffer commandBuffer, OverlayLoadOp loadOp = OverlayLoadOp::Load);
        void endOverlayRenderPass(VkCommandBuffer commandBuffer) const;
        void setSceneOutputImage(VkImageView imageView, VkImageLayout imageLayout, VkExtent2D extent);
        void setSceneViewportExtent(VkExtent2D extent) { sceneViewportExtent = extent; }

        Settings settings;
    private:
        void createCommandBuffers();
        void freeCommandBuffers();
        void recreateSwapChain();
        void createComputeSyncObjects();

        VkCommandBuffer getCurrentGraphicsCommandBuffer() const {
            assert(isFrameStarted && "Cannot get command buffer when frame is not in progress");
            return graphicsCommandBuffers_[currentFrameIndex];
        }

        VkCommandBuffer getCurrentComputeCommandBuffer() const {
            assert(isFrameStarted && "Cannot get command buffer when frame is not in progress");
            return computeCommandBuffers[currentFrameIndex];
        }

        std::unique_ptr<Window> window_;
        std::unique_ptr<Device> device_;
        std::shared_ptr<SwapChain> swapChain_;
        SceneOutputImage sceneOutputImage;
        VkExtent2D sceneViewportExtent{};
        std::vector<VkCommandBuffer> graphicsCommandBuffers_;

        uint32_t currentImageIndex{};
        int currentFrameIndex{0};
        bool isFrameStarted{false};

        std::vector<VkCommandBuffer> computeCommandBuffers;
        std::vector<VkFence> computeInFlightFences;
        std::vector<VkSemaphore> computeFinishedSemaphores;
    };
}
