#include "FramebufferExporter.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <vector>

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include "core/Log.hpp"
#include "renderer/Renderer.hpp"
#include "renderer/abstraction/GPUBuffer.hpp"

namespace Atlas::Editor {
    bool isSupportedFormat(VkFormat format) {
        return format == VK_FORMAT_R8G8B8A8_SRGB ||
               format == VK_FORMAT_R8G8B8A8_UNORM ||
               format == VK_FORMAT_B8G8R8A8_SRGB ||
               format == VK_FORMAT_B8G8R8A8_UNORM;
    }

    bool isBgraFormat(VkFormat format) {
        return format == VK_FORMAT_B8G8R8A8_SRGB ||
               format == VK_FORMAT_B8G8R8A8_UNORM;
    }

    std::filesystem::path withPngExtension(const std::string &path) {
        std::filesystem::path exportPath(path);
        if (exportPath.extension().empty()) {
            exportPath.replace_extension(".png");
        }
        return exportPath;
    }

    VkAccessFlags accessForLayout(VkImageLayout layout) {
        switch (layout) {
            case VK_IMAGE_LAYOUT_GENERAL: return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL: return VK_ACCESS_SHADER_READ_BIT;
            case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL: return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL: return VK_ACCESS_TRANSFER_READ_BIT;
            default: return VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        }
    }

    void transitionForReadback(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = accessForLayout(oldLayout);
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        vkCmdPipelineBarrier(
            cmd,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &barrier);
    }

    void restoreLayoutAfterReadback(VkCommandBuffer cmd, VkImage image, VkImageLayout layout) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = layout;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = accessForLayout(layout);
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        vkCmdPipelineBarrier(
            cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &barrier);
    }

    void copyImageToBuffer(VkCommandBuffer cmd, VkImage image, VkBuffer buffer, VkExtent2D extent) {
        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {extent.width, extent.height, 1};

        vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &region);
    }

    std::vector<uint8_t> readPixels(const GPUBuffer &staging, VkFormat format, VkExtent2D extent) {
        const size_t pixelCount = static_cast<size_t>(extent.width) * static_cast<size_t>(extent.height);
        const auto *source = static_cast<const uint8_t *>(staging.getMapped());

        std::vector<uint8_t> pixels(pixelCount * 4);
        if (isBgraFormat(format)) {
            for (size_t i = 0; i < pixelCount; ++i) {
                pixels[i * 4 + 0] = source[i * 4 + 2];
                pixels[i * 4 + 1] = source[i * 4 + 1];
                pixels[i * 4 + 2] = source[i * 4 + 0];
                pixels[i * 4 + 3] = source[i * 4 + 3];
            }
        } else {
            std::memcpy(pixels.data(), source, pixels.size());
        }

        return pixels;
    }

    bool FramebufferExporter::exportSceneOutput(Renderer &renderer, const std::string &path) {
        const Renderer::SceneOutputImage &output = renderer.getSceneOutputImage();
        if (!output.valid() || output.imageLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
            AT_WARN("FramebufferExporter: no scene framebuffer is ready to export");
            return false;
        }

        if (output.extent.width == 0 || output.extent.height == 0) {
            AT_WARN("FramebufferExporter: scene framebuffer has an empty extent");
            return false;
        }

        if (!isSupportedFormat(output.format)) {
            AT_WARN("FramebufferExporter: unsupported framebuffer format {}", static_cast<int>(output.format));
            return false;
        }

        if (output.extent.width > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
            output.extent.height > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
            AT_WARN("FramebufferExporter: framebuffer is too large to export");
            return false;
        }

        Device &device = renderer.device();
        const VkDeviceSize imageSize = static_cast<VkDeviceSize>(output.extent.width) *
                                       static_cast<VkDeviceSize>(output.extent.height) *
                                       4;

        GPUBuffer staging = GPUBuffer::Builder(device)
                .setSize(imageSize)
                .setUsage(VK_BUFFER_USAGE_TRANSFER_DST_BIT)
                .setMemoryUsage(VMA_MEMORY_USAGE_AUTO)
                .setAllocationFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT)
                .setMapped()
                .build();

        VkCommandBuffer cmd = device.beginGraphicsCommands();
        transitionForReadback(cmd, output.image, output.imageLayout);
        copyImageToBuffer(cmd, output.image, staging.get(), output.extent);
        restoreLayoutAfterReadback(cmd, output.image, output.imageLayout);
        device.endGraphicsCommands(cmd);

        staging.invalidate(imageSize);
        std::vector<uint8_t> pixels = readPixels(staging, output.format, output.extent);

        const std::filesystem::path exportPath = withPngExtension(path);
        const std::string exportPathString = exportPath.string();
        const int width = static_cast<int>(output.extent.width);
        const int height = static_cast<int>(output.extent.height);
        const int stride = width * 4;

        if (stbi_write_png(exportPathString.c_str(), width, height, 4, pixels.data(), stride) == 0) {
            AT_WARN("FramebufferExporter: failed to write '{}'", exportPathString);
            return false;
        }

        AT_INFO("FramebufferExporter: exported '{}'", exportPathString);
        return true;
    }
}
