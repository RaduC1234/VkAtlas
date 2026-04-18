#include "OutputPass.hpp"
#include "renderer/abstraction/GPUImage.hpp"

namespace Atlas {
    OutputPass::OutputPass(Device &device, Renderer &renderer) : device(device), renderer(renderer) {
    }

    void OutputPass::getDeclaredOutputs(std::vector<Resource::Description> &out) const {
        // Writes directly to the swapchain — no owned resource declared.
    }

    void OutputPass::getDeclaredInputs(std::vector<std::string> &out) const {
        out.push_back("post_color");
    }

    void OutputPass::onResourcesCreated(const std::unordered_map<std::string, std::reference_wrapper<Resource> > &resources) {
        postColorSource = &resources.at("post_color").get().asImage();
    }

    void OutputPass::record(VkCommandBuffer cmd, VkDescriptorSet /*globalSet*/) {
        VkImageMemoryBarrier barriers[2]{};

        // 1. post_color: SHADER_READ_ONLY → TRANSFER_SRC
        auto &srcBarrier = barriers[0];
        srcBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        srcBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        srcBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        srcBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        srcBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        srcBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        srcBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        srcBarrier.image = postColorSource->image();
        srcBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        // 2. Swapchain image: UNDEFINED → TRANSFER_DST
        //    Use UNDEFINED as oldLayout — we're overwriting every pixel so we
        //    don't need to preserve previous content, and it avoids a pointless
        //    cache flush on tilers.
        VkImage swapImage = renderer.getCurrentSwapchainImage();

        auto &dstBarrier = barriers[1];
        dstBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        dstBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        dstBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        dstBarrier.srcAccessMask = 0;
        dstBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        dstBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        dstBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        dstBarrier.image = swapImage;
        dstBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // post-process just wrote
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr,
                             2, barriers);

        // ------------------------------------------------------------------ //
        // Blit                                                                //
        // ------------------------------------------------------------------ //

        VkExtent2D srcExtent = postColorSource->extent();
        VkExtent2D dstExtent = renderer.getSwapchainExtent();

        VkImageBlit region{};
        region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.srcOffsets[0] = {0, 0, 0};
        region.srcOffsets[1] = {static_cast<int32_t>(srcExtent.width), static_cast<int32_t>(srcExtent.height), 1};
        region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.dstOffsets[0] = {0, 0, 0};
        region.dstOffsets[1] = {static_cast<int32_t>(dstExtent.width), static_cast<int32_t>(dstExtent.height), 1};

        vkCmdBlitImage(cmd,
                       postColorSource->image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       swapImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &region,
                       // LINEAR gives a cheap rescale if the window was resized and
                       // extents differ; NEAREST is fine when they match.
                       VK_FILTER_LINEAR);

        // ------------------------------------------------------------------ //
        // Swapchain image: TRANSFER_DST → PRESENT_SRC_KHR                    //
        // (post_color doesn't need restoring — it won't be read again until   //
        //  PostProcessPass re-renders it next frame with initialLayout=UNDEF) //
        // ------------------------------------------------------------------ //

        VkImageMemoryBarrier presentBarrier{};
        presentBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        presentBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        presentBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        presentBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        presentBarrier.dstAccessMask = 0;
        presentBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        presentBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        presentBarrier.image = swapImage;
        presentBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                             0, 0, nullptr, 0, nullptr,
                             1, &presentBarrier);
    }
} // namespace Atlas
