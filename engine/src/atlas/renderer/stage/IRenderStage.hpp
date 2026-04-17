#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include <entt/entity/registry.hpp>
#include <vulkan/vulkan.h>

namespace Atlas {
    class GPUImage;

    class IRenderStage {
    public:
        struct Resource {
            enum class Type { COLOR_ATTACHMENT, DEPTH_ATTACHMENT, SHADER_READ, SHADER_WRITE };

            std::string name;
            VkFormat format;
            VkImageUsageFlags usage;
            Type type;

            static Resource color(const char *name, VkFormat fmt = VK_FORMAT_R16G16B16A16_SFLOAT) {
                return {name, fmt, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, Type::COLOR_ATTACHMENT};
            }

            static Resource depth(const char *name, VkFormat fmt = VK_FORMAT_D32_SFLOAT_S8_UINT) {
                return {name, fmt, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, Type::DEPTH_ATTACHMENT};
            }
        };

        virtual ~IRenderStage() = default;

        virtual void getDeclaredOutputs(std::vector<Resource> &out) const = 0;
        virtual void getDeclaredInputs(std::vector<std::string> &out) const = 0;

        virtual void onResourcesCreated(const std::unordered_map<std::string, std::reference_wrapper<GPUImage>>& resources) = 0;
        virtual void onSceneChanged(entt::registry& registry) {}

        virtual void record(VkCommandBuffer cmd, VkDescriptorSet globalSet) = 0;
    };
} // Atlas
