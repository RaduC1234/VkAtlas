#include "Renderer.hpp"

// std
#include <array>
#include <cassert>
#include <stdexcept>

#include "entity/Object.hpp"

namespace Atlas {
    Renderer::Renderer(Window &window, Device &device) : window(window), device(device) {
        recreateSwapChain();
        createCommandBuffers();
        createComputeSyncObjects();
        createImGuiLayer();
    }

    Renderer::~Renderer() {
        for (size_t i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; i++) {
            vkDestroyFence(device.device(), computeInFlightFences[i], nullptr);
            vkDestroySemaphore(device.device(), computeFinishedSemaphores[i], nullptr); // missing
        }

        freeCommandBuffers();
    }

    void Renderer::recreateSwapChain() {
        auto extent = window.getExtent();
        while (extent.width == 0 || extent.height == 0) {
            extent = window.getExtent();
            //window->waitEvents();
        }
        vkDeviceWaitIdle(device.device());

        if (swapChain == nullptr) {
            swapChain = std::make_unique<SwapChain>(device, extent);
        } else {
            std::shared_ptr oldOldSwapChain = std::move(swapChain);
            swapChain = std::make_unique<SwapChain>(device, extent, oldOldSwapChain);

            if (!oldOldSwapChain->compareSwapFormats(*swapChain)) {
                throw std::runtime_error("Swap chain image(or depth) format has changed!");
            }
        }
    }

    FrameContext Renderer::beginFrame() {
        assert(!isFrameStarted && "Can't call beginFrame while already in progress");

        auto result = swapChain->acquireNextImage(&currentImageIndex);

        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            recreateSwapChain();
            return {};
        }

        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            throw std::runtime_error("failed to acquire swap chain image!");
        }

        isFrameStarted = true;

        auto graphicsCommandBuffer = getCurrentGraphicsCommandBuffer();
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        if (vkBeginCommandBuffer(graphicsCommandBuffer, &beginInfo) != VK_SUCCESS) {
            throw std::runtime_error("failed to begin recording command buffer!");
        }

        vkWaitForFences(device.device(), 1, &computeInFlightFences[currentFrameIndex], VK_TRUE, UINT64_MAX);
        vkResetFences(device.device(), 1, &computeInFlightFences[currentFrameIndex]);

        auto computeCommandBuffer = getCurrentComputeCommandBuffer();
        vkResetCommandBuffer(computeCommandBuffer, 0);
        if (vkBeginCommandBuffer(computeCommandBuffer, &beginInfo) != VK_SUCCESS) {
            throw std::runtime_error("failed to begin recording compute command buffer!");
        }

        this->imGuiLayer->beginFrame();

        return {
            .graphicsCommandBuffer = graphicsCommandBuffer,
            .computeCommandBuffer = computeCommandBuffer,
            .index = static_cast<uint32_t>(currentFrameIndex)
        };
    }

    void Renderer::endFrame() {
        assert(isFrameStarted && "Can't call endFrame while not in progress");
        auto graphicsCommandBuffer = getCurrentGraphicsCommandBuffer();
        auto computeCommandBuffer = getCurrentComputeCommandBuffer();
        const bool sameFamily = device.graphicsQueue() == device.computeQueue();

        VkClearValue clear{};
        clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

        VkRenderPassBeginInfo rpInfo{};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpInfo.renderPass = swapChain->getImGuiRenderPass();
        rpInfo.framebuffer = swapChain->getImGuiFrameBuffer(currentImageIndex);
        rpInfo.renderArea.offset = {0, 0};
        rpInfo.renderArea.extent = swapChain->getSwapChainExtent();
        rpInfo.clearValueCount = 1;
        rpInfo.pClearValues = &clear;

        vkCmdBeginRenderPass(graphicsCommandBuffer, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
        imGuiLayer->endFrame(graphicsCommandBuffer);
        vkCmdEndRenderPass(graphicsCommandBuffer);

        if (vkEndCommandBuffer(graphicsCommandBuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to record command buffer!");
        }

        std::optional<VkSemaphore> computeFinishedSemaphore;
        if (!sameFamily) {
            if (vkEndCommandBuffer(computeCommandBuffer) != VK_SUCCESS) {
                throw std::runtime_error("failed to record compute command buffer!");
            }

            VkSubmitInfo computeSubmit{};
            computeSubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            computeSubmit.commandBufferCount = 1;
            computeSubmit.pCommandBuffers = &computeCommandBuffer;
            computeSubmit.signalSemaphoreCount = 1;
            computeSubmit.pSignalSemaphores = &computeFinishedSemaphores[currentFrameIndex];

            if (vkQueueSubmit(device.computeQueue(), 1, &computeSubmit, computeInFlightFences[currentFrameIndex]) != VK_SUCCESS) {
                throw std::runtime_error("failed to submit compute command buffer!");
            }

            computeFinishedSemaphore = computeFinishedSemaphores[currentFrameIndex];
        }

        auto result = swapChain->submitCommandBuffers(graphicsCommandBuffer, computeFinishedSemaphore, &currentImageIndex);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || window.wasWindowResized()) {
            window.resetWindowResizedFlag();
            recreateSwapChain();
        } else if (result != VK_SUCCESS) {
            throw std::runtime_error("failed to present swap chain image!");
        }

        isFrameStarted = false;
        currentFrameIndex = (currentFrameIndex + 1) % SwapChain::MAX_FRAMES_IN_FLIGHT;
    }

    void Renderer::beginSwapChainRenderPass(VkCommandBuffer commandBuffer) {
        assert(isFrameStarted && "Can't call beginSwapChainRenderPass if frame is not in progress");
        assert(commandBuffer == getCurrentGraphicsCommandBuffer() && "Can't begin render pass on command buffer from a different frame");

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = swapChain->getRenderPass();
        renderPassInfo.framebuffer = swapChain->getFrameBuffer(static_cast<int32_t>(currentImageIndex));

        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = swapChain->getSwapChainExtent();

        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = {0.0151f, 0.0151f, 0.0151f, 1.0f};
        clearValues[1].depthStencil = {1.0f, 0};
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(swapChain->getSwapChainExtent().width);
        viewport.height = static_cast<float>(swapChain->getSwapChainExtent().height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        VkRect2D scissor{{0, 0}, swapChain->getSwapChainExtent()};
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    }

    void Renderer::endSwapChainRenderPass(VkCommandBuffer commandBuffer) const {
        assert(isFrameStarted && "Can't call endSwapChainRenderPass if frame is not in progress");
        assert(commandBuffer == getCurrentGraphicsCommandBuffer() && "Can't end render pass on command buffer from a different frame");

        // imGuiLayer->endFrame(commandBuffer);
        vkCmdEndRenderPass(commandBuffer);
    }

    void Renderer::createComputeSyncObjects() {
        computeInFlightFences.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
        computeFinishedSemaphores.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        VkSemaphoreCreateInfo semInfo{};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        for (size_t i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; i++) {
            if (vkCreateFence(device.device(), &fenceInfo, nullptr, &computeInFlightFences[i]) != VK_SUCCESS) {
                throw std::runtime_error("failed to create compute fence!");
            }

            if (vkCreateSemaphore(device.device(), &semInfo, nullptr, &computeFinishedSemaphores[i]) != VK_SUCCESS) {
                throw std::runtime_error("failed to create compute semaphore!");
            }
        }
    }

    void Renderer::createImGuiLayer() {
        this->imGuiLayer = std::make_unique<ImGuiLayer>(
            device,
            window,
            swapChain->getImGuiRenderPass(),
            static_cast<uint32_t>(getImageCount())
        );
    }

    void Renderer::createCommandBuffers() {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;

        graphicsCommandBuffers_.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = device.getGraphicsCommandPool();
        allocInfo.commandBufferCount = static_cast<uint32_t>(graphicsCommandBuffers_.size());

        if (vkAllocateCommandBuffers(device.device(), &allocInfo, graphicsCommandBuffers_.data()) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate command buffers!");
        }

        computeCommandBuffers.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
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
