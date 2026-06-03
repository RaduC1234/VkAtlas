#include "Renderer.hpp"

// std
#include <array>
#include <cassert>
#include <stdexcept>

#include "Device.hpp"
#include "core/Profiler.hpp"
#include "entity/Object.hpp"

namespace Atlas {
    Renderer::Renderer(const CreateInfo &createInfo) : createInfo(createInfo) {
        this->window_ = Window::create(createInfo.window);
        this->device_ = std::make_unique<Device>(*window_, Device::CreateInfo{createInfo.enableRaytracing});
        this->resourceManager_ = std::make_unique<ResourceManager>(*device_);

        recreateSwapChain();
        createCommandBuffers();
        createComputeSyncObjects();
    }

    Renderer::~Renderer() {
        vkDeviceWaitIdle(device_->device());

        for (size_t i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; i++) {
            vkDestroyFence(device_->device(), computeInFlightFences[i], nullptr);
            vkDestroySemaphore(device_->device(), computeFinishedSemaphores[i], nullptr); // missing
        }

        freeCommandBuffers();
    }

    float Renderer::getAspectRatio() const {
        if (sceneViewportExtent.width > 0 &&
            sceneViewportExtent.height > 0) {
            return static_cast<float>(sceneViewportExtent.width) / static_cast<float>(sceneViewportExtent.height);
        }

        return swapChain_->extentAspectRatio();
    }

    VkCommandBuffer Renderer::currentGraphicsCommandBuffer() const {
        return getCurrentGraphicsCommandBuffer();
    }

    void Renderer::setSceneOutputImage(VkImage image, VkImageView imageView, VkImageLayout imageLayout, VkFormat format, VkExtent2D extent) {
        sceneOutputImage = {
            .image = image,
            .imageView = imageView,
            .imageLayout = imageLayout,
            .format = format,
            .extent = extent
        };
    }

    void Renderer::clearSceneOutputImage() {
        sceneOutputImage = {};
    }

    FrameContext Renderer::beginFrame() {
        ATLAS_PROFILE_SCOPE("Renderer::beginFrame");
        assert(!isFrameStarted && "Can't call beginFrame while already in progress");

        VkResult result = VK_SUCCESS;
        {
            ATLAS_PROFILE_SCOPE("Renderer::acquireNextImage");
            result = swapChain_->acquireNextImage(&currentImageIndex);
        }

        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            recreateSwapChain();
            return {};
        }

        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            throw std::runtime_error("failed to acquire swap chain image!");
        }

        isFrameStarted = true;

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        auto graphicsCommandBuffer = getCurrentGraphicsCommandBuffer();
        if (vkBeginCommandBuffer(graphicsCommandBuffer, &beginInfo) != VK_SUCCESS) {
            throw std::runtime_error("failed to begin recording command buffer!");
        }
        ATLAS_PROFILE_GPU_ZONE(device_->gpuProfilerContext(), graphicsCommandBuffer, "Renderer::BeginFrame");

        auto computeCommandBuffer = getCurrentComputeCommandBuffer();
        vkResetCommandBuffer(computeCommandBuffer, 0);
        if (vkBeginCommandBuffer(computeCommandBuffer, &beginInfo) != VK_SUCCESS) {
            throw std::runtime_error("failed to begin recording compute command buffer!");
        }

        return {
            .graphicsCommandBuffer = graphicsCommandBuffer,
            .computeCommandBuffer = computeCommandBuffer,
            .index = static_cast<uint32_t>(currentFrameIndex)
        };
    }

    void Renderer::endFrame() {
        ATLAS_PROFILE_SCOPE("Renderer::endFrame");
        assert(isFrameStarted && "Can't call endFrame while not in progress");

        auto graphicsCommandBuffer = getCurrentGraphicsCommandBuffer();
        auto computeCommandBuffer = getCurrentComputeCommandBuffer();

        ATLAS_PROFILE_GPU_COLLECT(device_->gpuProfilerContext(), graphicsCommandBuffer);

        if (vkEndCommandBuffer(graphicsCommandBuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to record command buffer!");
        }

        // Always end compute — it was always begun in beginFrame()
        if (vkEndCommandBuffer(computeCommandBuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to record compute command buffer!");
        }

        VkResult result = VK_SUCCESS;
        {
            ATLAS_PROFILE_SCOPE("Renderer::submitAndPresent");
            result = swapChain_->submitCommandBuffers(graphicsCommandBuffer, {}, &currentImageIndex);
        }

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || window_->wasWindowResized()) {
            window_->resetWindowResizedFlag();
            recreateSwapChain();
        } else if (result != VK_SUCCESS) {
            throw std::runtime_error("failed to present swap chain image!");
        }

        isFrameStarted = false;
        currentFrameIndex = (currentFrameIndex + 1) % SwapChain::MAX_FRAMES_IN_FLIGHT;
    }

    void Renderer::beginSwapChainRenderPass(VkCommandBuffer commandBuffer) {
        ATLAS_PROFILE_SCOPE("Renderer::beginSwapChainRenderPass");
        assert(isFrameStarted && "Can't call beginSwapChainRenderPass if frame is not in progress");
        assert(commandBuffer == getCurrentGraphicsCommandBuffer() && "Can't begin render pass on command buffer from a different frame");

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = swapChain_->getRenderPass();
        renderPassInfo.framebuffer = swapChain_->getFrameBuffer(static_cast<int32_t>(currentImageIndex));

        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = swapChain_->getSwapChainExtent();

        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = {0.0151f, 0.0151f, 0.0151f, 1.0f};
        clearValues[1].depthStencil = {1.0f, 0};
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(swapChain_->getSwapChainExtent().width);
        viewport.height = static_cast<float>(swapChain_->getSwapChainExtent().height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        VkRect2D scissor{{0, 0}, swapChain_->getSwapChainExtent()};
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    }

    void Renderer::endSwapChainRenderPass(VkCommandBuffer commandBuffer) const {
        ATLAS_PROFILE_SCOPE("Renderer::endSwapChainRenderPass");
        assert(isFrameStarted && "Can't call endSwapChainRenderPass if frame is not in progress");
        assert(commandBuffer == getCurrentGraphicsCommandBuffer() && "Can't end render pass on command buffer from a different frame");

        vkCmdEndRenderPass(commandBuffer);
    }

    void Renderer::beginOverlayRenderPass(VkCommandBuffer commandBuffer, OverlayLoadOp loadOp) {
        ATLAS_PROFILE_SCOPE("Renderer::beginOverlayRenderPass");
        assert(isFrameStarted && "Can't call beginOverlayRenderPass if frame is not in progress");
        assert(commandBuffer == getCurrentGraphicsCommandBuffer() && "Can't begin overlay render pass on command buffer from a different frame");

        VkClearValue clear{};
        clear.color = {{0.005f, 0.006f, 0.008f, 1.0f}};

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        const bool clearOverlay = loadOp == OverlayLoadOp::Clear;
        renderPassInfo.renderPass = getOverlayRenderPass(loadOp);
        renderPassInfo.framebuffer = clearOverlay ? swapChain_->getOverlayClearFrameBuffer(currentImageIndex) : swapChain_->getOverlayFrameBuffer(currentImageIndex);
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = swapChain_->getSwapChainExtent();
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clear;

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    }

    void Renderer::endOverlayRenderPass(VkCommandBuffer commandBuffer) const {
        ATLAS_PROFILE_SCOPE("Renderer::endOverlayRenderPass");
        assert(isFrameStarted && "Can't call endOverlayRenderPass if frame is not in progress");
        assert(commandBuffer == getCurrentGraphicsCommandBuffer() && "Can't end overlay render pass on command buffer from a different frame");

        vkCmdEndRenderPass(commandBuffer);
    }

    void Renderer::createCommandBuffers() {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;

        graphicsCommandBuffers_.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = device_->getGraphicsCommandPool();
        allocInfo.commandBufferCount = static_cast<uint32_t>(graphicsCommandBuffers_.size());

        if (vkAllocateCommandBuffers(device_->device(), &allocInfo, graphicsCommandBuffers_.data()) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate command buffers!");
        }

        computeCommandBuffers.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = device_->getComputeCommandPool();
        allocInfo.commandBufferCount = static_cast<uint32_t>(computeCommandBuffers.size());

        if (vkAllocateCommandBuffers(device_->device(), &allocInfo, computeCommandBuffers.data()) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate compute command buffers!");
        }
    }

    void Renderer::freeCommandBuffers() {
        vkFreeCommandBuffers(
            device_->device(),
            device_->getGraphicsCommandPool(),
            static_cast<uint32_t>(graphicsCommandBuffers_.size()),
            graphicsCommandBuffers_.data()
        );
        graphicsCommandBuffers_.clear();

        vkFreeCommandBuffers(
            device_->device(),
            device_->getComputeCommandPool(),
            static_cast<uint32_t>(computeCommandBuffers.size()),
            computeCommandBuffers.data()
        );
        computeCommandBuffers.clear();
    }

    void Renderer::recreateSwapChain() {
        ATLAS_PROFILE_SCOPE("Renderer::recreateSwapChain");
        auto extent = window_->getExtent();
        while (extent.width == 0 || extent.height == 0) {
            extent = window_->getExtent();
            //window_->waitEvents();
        }
        vkDeviceWaitIdle(device_->device());

        if (swapChain_ == nullptr) {
            swapChain_ = std::make_shared<SwapChain>(*device_, extent);
        } else {
            auto oldSwapChain = std::move(swapChain_);
            swapChain_ = std::make_shared<SwapChain>(*device_, extent, oldSwapChain);

            if (!oldSwapChain->compareSwapFormats(*swapChain_)) {
                throw std::runtime_error("Swap chain image(or depth) format has changed!");
            }
        }
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
            if (vkCreateFence(device_->device(), &fenceInfo, nullptr, &computeInFlightFences[i]) != VK_SUCCESS) {
                throw std::runtime_error("failed to create compute fence!");
            }

            if (vkCreateSemaphore(device_->device(), &semInfo, nullptr, &computeFinishedSemaphores[i]) != VK_SUCCESS) {
                throw std::runtime_error("failed to create compute semaphore!");
            }
        }
    }
}
