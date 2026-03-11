#pragma once

#include "Device.hpp"
#include "SwapChain.hpp"
#include "core/Window.hpp"

#include <cassert>

namespace Atlas {
    class Renderer {
    public:
        Renderer(Window &window, Device &device);
        ~Renderer();

        // Not copyable
        Renderer(const Renderer &) = delete;
        Renderer &operator=(const Renderer &) = delete;

        // Window & device accessors
        Window &getWindow() const { return window; }
        Device &getDevice() const { return device; }

        // Swap chain accessors
        VkRenderPass getSwapChainRenderPass() const { return swapChain->getRenderPass(); }
        float getAspectRatio() const { return swapChain->extentAspectRatio(); }
        size_t getImageCount() const { return swapChain->imageCount(); }

        // Frame state
        bool isFrameInProgress() const { return isFrameStarted; }

        int getFrameIndex() const {
            assert(isFrameStarted && "Cannot get frame index when frame not in progress");
            return currentFrameIndex;
        }

        VkCommandBuffer getCurrentGraphicsCommandBuffer() const {
            assert(isFrameStarted && "Cannot get command buffer when frame not in progress");
            return graphicsCommandBuffers_[currentFrameIndex];
        }

        VkCommandBuffer getCurrentComputeCommandBuffer() const {
            assert(isComputeStarted && "Cannot get command buffer when compute not in progress");
            return computeCommandBuffers[currentFrameIndex];
        }

        // Frame lifecycle
        VkCommandBuffer beginFrame();
        void endFrame();

        void beginSwapChainRenderPass(VkCommandBuffer);
        void endSwapChainRenderPass(VkCommandBuffer) const;

        // Compute lifecycle
        VkCommandBuffer beginCompute();
        void endCompute();

    private:
        void createCommandBuffers();
        void freeCommandBuffers();
        void recreateSwapChain();
        void createComputeSyncObjects();

        Window &window;
        Device &device;
        std::unique_ptr<SwapChain> swapChain;
        std::vector<VkCommandBuffer> graphicsCommandBuffers_;

        uint32_t currentImageIndex;
        int currentFrameIndex{0};
        bool isFrameStarted{false};

        bool isComputeStarted{false};
        std::vector<VkCommandBuffer> computeCommandBuffers;
        std::vector<VkFence> computeInFlightFences;
    };
}