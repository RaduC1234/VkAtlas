#pragma once
#include <vulkan/vulkan.h>
#include <vector>

namespace Atlas {
    struct PassResource {
        enum class Type { ColorAttachment, DepthAttachment, ShaderRead, ShaderWrite };
        enum class Access { Read, Write };

        VkImage *image;
        Type type;
        Access access;
        const char *debugName;
    };

    class IRenderPass {
    public:
        virtual ~IRenderPass() = default;

        virtual void begin(VkCommandBuffer cmd) = 0;
        virtual void end(VkCommandBuffer cmd) = 0;
        virtual void barrier(VkCommandBuffer cmd) = 0;
        virtual void getDeclaredResources(std::vector<PassResource> &out) const = 0;
    };
} // Atlas
