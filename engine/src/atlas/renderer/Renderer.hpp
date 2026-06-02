#pragma once

#include "Device.hpp"
#include "ResourceManager.hpp"

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

    enum class ViewMode : uint32_t {
        LIT,
        UNLIT,
        LIGHTING_ONLY,
        PATH_TRACING
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
            VkImage image = VK_NULL_HANDLE;
            VkImageView imageView = VK_NULL_HANDLE;
            VkImageLayout imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            VkFormat format = VK_FORMAT_UNDEFINED;
            VkExtent2D extent{};

            bool valid() const { return image != VK_NULL_HANDLE && imageView != VK_NULL_HANDLE; }
        };

        struct CreateInfo {
            Window::CreateInfo window;
            bool enableRaytracing = false;
            SceneOutputTarget sceneOutputTarget = SceneOutputTarget::Swapchain;
        };

        Renderer(const CreateInfo &createInfo);
        ~Renderer();

        Renderer(const Renderer &) = delete;
        Renderer &operator=(const Renderer &) = delete;

        Window &window() const { return *window_; }
        Device &device() const { return *device_; }
        ResourceManager &resourceManager() { return *resourceManager_; }
        const ResourceManager &resourceManager() const { return *resourceManager_; }

        VkRenderPass getSwapChainRenderPass() const { return swapChain_->getRenderPass(); }
        VkRenderPass getOverlayRenderPass(OverlayLoadOp loadOp = OverlayLoadOp::Load) const {
            return loadOp == OverlayLoadOp::Clear ? swapChain_->getOverlayClearRenderPass() : swapChain_->getOverlayRenderPass();
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
        void setSceneOutputImage(VkImage image, VkImageView imageView, VkImageLayout imageLayout, VkFormat format, VkExtent2D extent);
        void setSceneViewportExtent(VkExtent2D extent) { sceneViewportExtent = extent; }

        CreateInfo createInfo;
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
        std::unique_ptr<ResourceManager> resourceManager_;

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
