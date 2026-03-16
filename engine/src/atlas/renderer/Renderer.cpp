#include "Renderer.hpp"

// std
#include <array>
#include <cassert>
#include <stdexcept>

#include "XrSwapChain.hpp"
#include "entity/Object.hpp"

namespace Atlas {
    Renderer::Renderer(Window &window, Device &device) : window(window), device(device) {
        recreateSwapChain();
        createCommandBuffers();
        createComputeSyncObjects();
    }

    Renderer::~Renderer() {
        for (auto fence: computeInFlightFences) {
            vkDestroyFence(device.device(), fence, nullptr);
        }
        freeCommandBuffers();
    }

    void Renderer::recreateSwapChain() {
        std::shared_ptr<WindowSwapChain> oldWindowSwapChain;
        for (auto &swapChain: swapChains) {
            if (auto *w = dynamic_cast<WindowSwapChain *>(swapChain.get())) {
                swapChain.release();
                oldWindowSwapChain = std::shared_ptr<WindowSwapChain>(w);
                break;
            }
        }

        swapChains.clear();
        const auto renderMode = device.getRenderMode();

        if (renderMode == RenderMode::WindowOnly || renderMode == RenderMode::Combined) {
            auto extent = window.getExtent();
            while (extent.width == 0 || extent.height == 0) {
                extent = window.getExtent();
                //window->waitEvents();
            }
            vkDeviceWaitIdle(device.device());

            std::unique_ptr<WindowSwapChain> w = oldWindowSwapChain ? std::make_unique<WindowSwapChain>(device, extent, oldWindowSwapChain) : std::make_unique<WindowSwapChain>(device, extent);
            if (oldWindowSwapChain && !w->compareSwapFormats(*oldWindowSwapChain)) {
                throw std::runtime_error("Swap chain image format has changed after recreation");
            }

            swapChains.push_back(std::move(w));
        }

        if (renderMode == RenderMode::XROnly || renderMode == RenderMode::Combined) {
            swapChains.push_back(std::make_unique<XrSwapChain>(device, device.getXrSession(), device.getXrViewConfigurationViews()));
        }
    }

    VkCommandBuffer Renderer::beginFrame() {
        assert(!isFrameStarted && "Can't call beginFrame while already in progress");

        auto result = swapChains[0]->acquireNextImage(&currentImageIndex);

        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            recreateSwapChain();
            return VK_NULL_HANDLE;
        }

        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            throw std::runtime_error("failed to acquire swap chain image!");
        }

        isFrameStarted = true;

        auto commandBuffer = getCurrentGraphicsCommandBuffer();

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
            throw std::runtime_error("failed to begin recording command buffer!");
        }

        return commandBuffer;
    }

    void Renderer::endFrame() {
        assert(isFrameStarted && "Can't call endFrame while not in progress");
        auto commandBuffer = getCurrentGraphicsCommandBuffer();

        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to record command buffer!");
        }

        auto result = swapChains[0]->submitCommandBuffers(&commandBuffer, &currentImageIndex);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || window.wasWindowResized()) {
            window.resetWindowResizedFlag();
            recreateSwapChain();
        } else if (result != VK_SUCCESS) {
            throw std::runtime_error("failed to present swap chain image!");
        }

        isFrameStarted = false;
        currentFrameIndex = (currentFrameIndex + 1) % WindowSwapChain::MAX_FRAMES_IN_FLIGHT;
    }

    void Renderer::beginSwapChainRenderPass(VkCommandBuffer commandBuffer) {
        assert(isFrameStarted && "Can't call beginSwapChainRenderPass if frame is not in progress");
        assert(commandBuffer == getCurrentGraphicsCommandBuffer() && "Can't begin render pass on command buffer from a different frame");

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = swapChains[0]->getRenderPass();
        renderPassInfo.framebuffer = swapChains[0]->getFrameBuffer(static_cast<int32_t>(currentImageIndex));

        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = swapChains[0]->getExtent();

        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = {0.0151f, 0.0151f, 0.0151f, 1.0f};
        clearValues[1].depthStencil = {1.0f, 0};
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(swapChains[0]->getExtent().width);
        viewport.height = static_cast<float>(swapChains[0]->getExtent().height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        VkRect2D scissor{{0, 0}, swapChains[0]->getExtent()};
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    }

    void Renderer::endSwapChainRenderPass(VkCommandBuffer commandBuffer) const {
        assert(isFrameStarted && "Can't call endSwapChainRenderPass if frame is not in progress");
        assert(commandBuffer == getCurrentGraphicsCommandBuffer() && "Can't end render pass on command buffer from a different frame");

        vkCmdEndRenderPass(commandBuffer);
    }

    VkCommandBuffer Renderer::beginCompute() {
        assert(!isComputeStarted && "Can't call beginCompute while already in progress");

        // Wait for previous compute work on this frame to complete
        vkWaitForFences(device.device(), 1, &computeInFlightFences[currentFrameIndex], VK_TRUE, UINT64_MAX);
        vkResetFences(device.device(), 1, &computeInFlightFences[currentFrameIndex]);

        isComputeStarted = true;

        auto commandBuffer = getCurrentComputeCommandBuffer();

        // Reset the command buffer before recording
        vkResetCommandBuffer(commandBuffer, 0);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
            throw std::runtime_error("failed to begin recording compute command buffer!");
        }

        return commandBuffer;
    }

    void Renderer::endCompute() {
        assert(isComputeStarted && "Can't call endCompute while not in progress");

        auto commandBuffer = getCurrentComputeCommandBuffer();

        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to record compute command buffer!");
        }

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        // Submit with fence - non-blocking, fence will be signaled when GPU completes
        if (vkQueueSubmit(device.computeQueue(), 1, &submitInfo, computeInFlightFences[currentFrameIndex]) != VK_SUCCESS) {
            throw std::runtime_error("failed to submit compute command buffer!");
        }

        isComputeStarted = false;
    }

    void Renderer::createComputeSyncObjects() {
        computeInFlightFences.resize(WindowSwapChain::MAX_FRAMES_IN_FLIGHT);

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // Start signaled so first frame doesn't block

        for (size_t i = 0; i < WindowSwapChain::MAX_FRAMES_IN_FLIGHT; i++) {
            if (vkCreateFence(device.device(), &fenceInfo, nullptr, &computeInFlightFences[i]) != VK_SUCCESS) {
                throw std::runtime_error("failed to create compute fence!");
            }
        }
    }

    void Renderer::createCommandBuffers() {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;

        graphicsCommandBuffers_.resize(WindowSwapChain::MAX_FRAMES_IN_FLIGHT);
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = device.getGraphicsCommandPool();
        allocInfo.commandBufferCount = static_cast<uint32_t>(graphicsCommandBuffers_.size());

        if (vkAllocateCommandBuffers(device.device(), &allocInfo, graphicsCommandBuffers_.data()) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate command buffers!");
        }

        computeCommandBuffers.resize(WindowSwapChain::MAX_FRAMES_IN_FLIGHT);
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = device.getComputeCommandPool();
        allocInfo.commandBufferCount = static_cast<uint32_t>(computeCommandBuffers.size());

        if (vkAllocateCommandBuffers(device.device(), &allocInfo, computeCommandBuffers.data()) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate compute command buffers!");
        }
    }

    void Renderer::freeCommandBuffers() {
        vkFreeCommandBuffers(
            device.device(),
            device.getGraphicsCommandPool(),
            static_cast<uint32_t>(graphicsCommandBuffers_.size()),
            graphicsCommandBuffers_.data());
        graphicsCommandBuffers_.clear();

        vkFreeCommandBuffers(
            device.device(),
            device.getComputeCommandPool(),
            static_cast<uint32_t>(computeCommandBuffers.size()),
            computeCommandBuffers.data());
        computeCommandBuffers.clear();
    }
}
