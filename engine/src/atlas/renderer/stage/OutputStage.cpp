#include "OutputStage.hpp"
#include "renderer/Renderer.hpp"
#include "renderer/abstraction/GPUImage.hpp"

namespace Atlas {
    OutputStage::OutputStage(Device &device, Renderer &renderer)
        : IRenderStage(Queue::GRAPHICS), device(device), renderer(renderer) {
    }

    void OutputStage::getDeclaredOutputs(std::vector<Resource::Description> &out) const {
    }

    void OutputStage::getDeclaredInputs(std::vector<std::string> &out) const {
        out.push_back("post_color");
    }

    void OutputStage::onResourcesCreated(const Context &ctx) {
        postColorSource = &ctx.resources.at("post_color").get().asImage();

        auto layoutIt = ctx.finalLayouts.find("post_color");
        if (layoutIt != ctx.finalLayouts.end()) {
            sourceLayout = layoutIt->second;
            restoreLayout = sourceLayout;
        }

        renderer.setSceneOutputImage(postColorSource->view(0), sourceLayout, postColorSource->extent());
    }

    void OutputStage::record(VkCommandBuffer cmd, VkDescriptorSet /*globalSet*/) {
        renderer.setSceneOutputImage(postColorSource->view(0), sourceLayout, postColorSource->extent());

        if (renderer.createInfo.sceneOutputTarget == Renderer::SceneOutputTarget::Texture) {
            recordToTexture(cmd);
        } else {
            recordToSwapChain(cmd);
        }
    }

    void OutputStage::recordToSwapChain(VkCommandBuffer cmd) {
        VkImage swapImage = renderer.getCurrentSwapchainImage();

        // sourceLayout is GENERAL for ray tracing / compute writers.
        // Use the broadest safe srcStage — covers compute, ray tracing, and raster.
        const VkPipelineStageFlags srcStage = (sourceLayout == VK_IMAGE_LAYOUT_GENERAL)
            ? (VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
            : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        const VkAccessFlags srcAccess = (sourceLayout == VK_IMAGE_LAYOUT_GENERAL)
            ? VK_ACCESS_SHADER_WRITE_BIT
            : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkImageMemoryBarrier pre[2]{};
        pre[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        pre[0].oldLayout = sourceLayout;
        pre[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        pre[0].srcAccessMask = srcAccess;
        pre[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        pre[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        pre[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        pre[0].image = postColorSource->image();
        pre[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        pre[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        pre[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        pre[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        pre[1].srcAccessMask = 0;
        pre[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        pre[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        pre[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        pre[1].image = swapImage;
        pre[1].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        vkCmdPipelineBarrier(cmd,
                             srcStage | VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 2, pre);

        const VkExtent2D src = postColorSource->extent();
        const VkExtent2D dst = renderer.getSwapchainExtent();
        VkImageBlit region{};
        region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.srcOffsets[1] = {(int32_t) src.width, (int32_t) src.height, 1};
        region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.dstOffsets[1] = {(int32_t) dst.width, (int32_t) dst.height, 1};

        vkCmdBlitImage(cmd,
                       postColorSource->image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       swapImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &region, VK_FILTER_LINEAR);

        // Post-blit: present the swapchain image and restore post_color for any later sampling.
        VkImageMemoryBarrier post[2]{};
        uint32_t postCount = 0;

        post[postCount].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        post[postCount].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        post[postCount].newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        post[postCount].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        post[postCount].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        post[postCount].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        post[postCount].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        post[postCount].image = swapImage;
        post[postCount].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        ++postCount;

        VkPipelineStageFlags postDst = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        if (restoreLayout != VK_IMAGE_LAYOUT_UNDEFINED) {
            post[postCount].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            post[postCount].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            post[postCount].newLayout = restoreLayout;
            post[postCount].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            post[postCount].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            post[postCount].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            post[postCount].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            post[postCount].image = postColorSource->image();
            post[postCount].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            ++postCount;
            if (restoreLayout == VK_IMAGE_LAYOUT_GENERAL) {
                post[postCount - 1].dstAccessMask |= VK_ACCESS_SHADER_WRITE_BIT;
                postDst |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
            }
        }

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, postDst,
                             0, 0, nullptr, 0, nullptr, postCount, post);
    }

    void OutputStage::recordToTexture(VkCommandBuffer cmd) {
        const bool sourceIsShaderWrite = sourceLayout == VK_IMAGE_LAYOUT_GENERAL;
        const VkPipelineStageFlags srcStage = sourceIsShaderWrite
                                                  ? (VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
                                                  : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        const VkAccessFlags srcAccess = sourceIsShaderWrite
                                            ? VK_ACCESS_SHADER_WRITE_BIT
                                            : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = sourceLayout;
        barrier.newLayout = sourceLayout;
        barrier.srcAccessMask = srcAccess;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = postColorSource->image();
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        vkCmdPipelineBarrier(cmd,
                             srcStage,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
    }
} // namespace Atlas
