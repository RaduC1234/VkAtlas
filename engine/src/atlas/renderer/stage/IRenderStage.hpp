#pragma once
#include <vulkan/vulkan.h>
#include <vector>

namespace Atlas {
    class IRenderStage {
    public:
        struct StageResource {
            enum class Type { COLOR_ATTACHMENT, DEPTH_ATTACHMENT, SHADER_READ, SHADER_WRITE };
            enum class Access { READ, WRITE };

            VkImage *image;
            Type type;
            Access access;
            const char *debugName;
        };

        virtual ~IRenderStage() = default;

        virtual void getDeclaredInputs(std::vector<StageResource> &out) const = 0;
        virtual void getDeclaredOutputs(std::vector<StageResource> &out) const = 0;

        virtual void record(VkCommandBuffer cmd, VkDescriptorSet globalSet) = 0;
    };
} // Atlas
