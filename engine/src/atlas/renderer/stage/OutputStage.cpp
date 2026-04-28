#include "OutputStage.hpp"
#include "renderer/abstraction/GPUImage.hpp"

namespace Atlas {
    OutputStage::OutputStage(Device &device, Renderer &renderer)
        : IRenderStage(Queue::GRAPHICS), device(device), renderer(renderer) {
    }

    void OutputStage::getDeclaredOutputs(std::vector<Resource::Description> &out) const {
        // Writes directly to the swapchain — no owned resource declared.
    }

    void OutputStage::getDeclaredInputs(std::vector<std::string> &out) const {
        out.push_back("post_color");
    }

    void OutputStage::onResourcesCreated(const Context &ctx) {
        postColorSource = &ctx.resources.at("post_color").get().asImage();

        auto layoutIt = ctx.finalLayouts.find("post_color");
        auto writerIt = ctx.lastWrittenBy.find("post_color");

        if (layoutIt != ctx.finalLayouts.end()) {
            sourceLayout = layoutIt->second;
            sourceIsCompute = writerIt != ctx.lastWrittenBy.end() &&
                              writerIt->second == Queue::COMPUTE;
            // Only restore the source layout when a compute stage owns it across
            // frames (e.g. PathTracingStage keeps post_color in GENERAL for
            // accumulation). Graphics writers (PostProcessPass) terminate their
            // render pass with finalLayout = SHADER_READ_ONLY_OPTIMAL every frame,
            // so the graph re-establishes the layout — no restore needed.
            restoreLayout = sourceIsCompute ? sourceLayout : VK_IMAGE_LAYOUT_UNDEFINED;
        }
    }

    void OutputStage::record(VkCommandBuffer cmd, VkDescriptorSet /*globalSet*/) {
        // ------------------------------------------------------------------ //
        // Pre-blit barriers                                                  //
        //                                                                    //
        // 1. post_color: <sourceLayout> → TRANSFER_SRC_OPTIMAL               //
        // 2. swapchain : UNDEFINED      → TRANSFER_DST_OPTIMAL               //
        //                                                                    //
        // We always discard the swapchain's previous content (UNDEFINED) —   //
        // the blit overwrites every pixel, so preserving the previous frame  //
        // would only cost a pointless cache flush on tilers.                 //
        // ------------------------------------------------------------------ //
        VkImage swapImage = renderer.getCurrentSwapchainImage();

        const VkPipelineStageFlags srcStage = sourceIsCompute
                                                  ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                                                  : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        const VkAccessFlags srcAccess = sourceIsCompute
                                            ? VK_ACCESS_SHADER_WRITE_BIT
                                            : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkImageMemoryBarrier preBarriers[2]{};

        auto &srcBarrier = preBarriers[0];
        srcBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        srcBarrier.oldLayout = sourceLayout;
        srcBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        srcBarrier.srcAccessMask = srcAccess;
        srcBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        srcBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        srcBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        srcBarrier.image = postColorSource->image();
        srcBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        auto &dstBarrier = preBarriers[1];
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
                             srcStage | VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr,
                             2, preBarriers);

        // ------------------------------------------------------------------ //
        // Blit                                                               //
        // ------------------------------------------------------------------ //
        const VkExtent2D srcExtent = postColorSource->extent();
        const VkExtent2D dstExtent = renderer.getSwapchainExtent();

        VkImageBlit region{};
        region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.srcOffsets[0] = {0, 0, 0};
        region.srcOffsets[1] = {static_cast<int32_t>(srcExtent.width),
                                static_cast<int32_t>(srcExtent.height), 1};
        region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.dstOffsets[0] = {0, 0, 0};
        region.dstOffsets[1] = {static_cast<int32_t>(dstExtent.width),
                                static_cast<int32_t>(dstExtent.height), 1};

        // LINEAR gives a cheap rescale when window/render extents differ;
        // it collapses to a copy when they match.
        vkCmdBlitImage(cmd,
                       postColorSource->image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       swapImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &region,
                       VK_FILTER_LINEAR);

        // ------------------------------------------------------------------ //
        // Post-blit barriers                                                 //
        //                                                                    //
        // 1. swapchain : TRANSFER_DST_OPTIMAL → PRESENT_SRC_KHR              //
        //                                                                    //
        //    The ImGui render pass uses initialLayout = PRESENT_SRC_KHR with //
        //    LOAD_OP_LOAD, so the swapchain image MUST be in PRESENT_SRC_KHR //
        //    when the render pass begins. Skipping this transition was the   //
        //    cause of VUID-vkCmdDraw-None-09600 firing every frame.          //
        //                                                                    //
        // 2. post_color: TRANSFER_SRC_OPTIMAL → <restoreLayout>  (optional)  //
        //                                                                    //
        //    Only emitted for compute writers — graphics writers re-enter    //
        //    their render pass each frame from UNDEFINED.                    //
        // ------------------------------------------------------------------ //
        VkImageMemoryBarrier postBarriers[2]{};
        uint32_t postBarrierCount = 0;

        // (1) swapchain → PRESENT_SRC_KHR
        auto &presentBarrier = postBarriers[postBarrierCount++];
        presentBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        presentBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        presentBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        presentBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        // The next consumer is the ImGui render pass (color attachment writes
        // load the existing content). Sync against COLOR_ATTACHMENT_OUTPUT.
        presentBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                       VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        presentBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        presentBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        presentBarrier.image = swapImage;
        presentBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        // (2) post_color → restoreLayout (compute writers only)
        VkPipelineStageFlags restoreDstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        if (restoreLayout != VK_IMAGE_LAYOUT_UNDEFINED) {
            auto &restoreBarrier = postBarriers[postBarrierCount++];
            restoreBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            restoreBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            restoreBarrier.newLayout = restoreLayout;
            restoreBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            restoreBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            restoreBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            restoreBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            restoreBarrier.image = postColorSource->image();
            restoreBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

            restoreDstStage |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        }

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             restoreDstStage,
                             0, 0, nullptr, 0, nullptr,
                             postBarrierCount, postBarriers);
    }
} // namespace Atlas