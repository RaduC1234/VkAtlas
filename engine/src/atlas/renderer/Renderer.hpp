#pragma once

#include "Device.hpp"
#include "ImGuiLayer.hpp"

#include "core/Window.hpp"
#include "swapchain/SwapChain.hpp"

namespace Atlas {
    struct FrameContext {
        VkCommandBuffer graphicsCommandBuffer;
        VkCommandBuffer computeCommandBuffer;
        uint32_t index;
    };

    class Renderer {
    public:
        struct Settings {
            Window::Settings windowSettings;
        };

        Renderer(const Settings &settings);
        ~Renderer();

        Renderer(const Renderer &) = delete;
        Renderer &operator=(const Renderer &) = delete;

        Window &window() const { return *window_; }
        Device &device() const { return *device_; }

        VkRenderPass getSwapChainRenderPass() const { return swapChain_->getRenderPass(); }
        float getAspectRatio() const { return swapChain_->extentAspectRatio(); }
        size_t getImageCount() const { return swapChain_->imageCount(); }
        VkImage getCurrentSwapchainImage() const { return swapChain_->getImage(currentImageIndex); }
        VkExtent2D getSwapchainExtent() const { return swapChain_->getSwapChainExtent(); }
        ImGuiLayer &getImGuiLayer() { return *imGuiLayer_; }

        FrameContext beginFrame();
        void endFrame();

        void beginSwapChainRenderPass(VkCommandBuffer);
        void endSwapChainRenderPass(VkCommandBuffer) const;

    private:
        void createCommandBuffers();
        void freeCommandBuffers();
        void recreateSwapChain();
        void createComputeSyncObjects();
        void createImGuiLayer();

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
        std::unique_ptr<ImGuiLayer> imGuiLayer_;
        std::shared_ptr<SwapChain> swapChain_;
        std::vector<VkCommandBuffer> graphicsCommandBuffers_;

        uint32_t currentImageIndex{};
        int currentFrameIndex{0};
        bool isFrameStarted{false};

        std::vector<VkCommandBuffer> computeCommandBuffers;
        std::vector<VkFence> computeInFlightFences;
        std::vector<VkSemaphore> computeFinishedSemaphores;
    };
}
