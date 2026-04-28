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
        Renderer(Window &window, Device &device);
        ~Renderer();

        Renderer(const Renderer &) = delete;
        Renderer &operator=(const Renderer &) = delete;

        Window &getWindow() const { return window; }
        Device &getDevice() const { return device; }

        VkRenderPass getSwapChainRenderPass() const { return swapChain->getRenderPass(); }
        float getAspectRatio() const { return swapChain->extentAspectRatio(); }
        size_t getImageCount() const { return swapChain->imageCount(); }
        VkImage getCurrentSwapchainImage() const { return swapChain->getImage(currentImageIndex); }
        VkExtent2D getSwapchainExtent() const { return swapChain->getSwapChainExtent(); }
        ImGuiLayer &getImGuiLayer() { return *imGuiLayer; }

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

        Window &window;
        Device &device;
        std::unique_ptr<ImGuiLayer> imGuiLayer;
        std::unique_ptr<SwapChain> swapChain;
        std::vector<VkCommandBuffer> graphicsCommandBuffers_;

        uint32_t currentImageIndex;
        int currentFrameIndex{0};
        bool isFrameStarted{false};

        std::vector<VkCommandBuffer> computeCommandBuffers;
        std::vector<VkFence> computeInFlightFences;
        std::vector<VkSemaphore> computeFinishedSemaphores;
    };
}
