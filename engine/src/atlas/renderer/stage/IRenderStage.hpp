#pragma once
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include <entt/entity/registry.hpp>
#include <vulkan/vulkan.h>

#include "renderer/abstraction/Buffer.hpp"
#include "renderer/abstraction/GPUImage.hpp"

namespace Atlas {
    class IRenderStage {
    public:
        class Resource {
        public:
            enum class Kind {
                IMAGE,
                BUFFER
            };

            enum class Type {
                INVALID,
                ATTACHMENT_COLOR,
                ATTACHMENT_DEPTH,
                SHADER_READ,
                SHADER_WRITE,
                BUFFER_VERTEX,
                BUFFER_INDEX,
                BUFFER_STORAGE
            };

            struct Description {
                std::string name;
                Type type;

                VkFormat format = VK_FORMAT_UNDEFINED;
                VkImageUsageFlags imageUsage = 0;
                uint32_t width = 0; // 0 = use graph extent
                uint32_t height = 0;

                VkBufferUsageFlags bufferUsage = 0;
                VkDeviceSize size = 0;
                bool hostVisible = false;

                Kind kind() const {
                    switch (type) {
                        case Type::ATTACHMENT_COLOR:
                        case Type::ATTACHMENT_DEPTH:
                        case Type::SHADER_READ:
                        case Type::SHADER_WRITE:
                            return Kind::IMAGE;
                        case Type::BUFFER_VERTEX:
                        case Type::BUFFER_INDEX:
                        case Type::BUFFER_STORAGE:
                            return Kind::BUFFER;
                        default:
                            throw std::runtime_error("Invalid resource type");
                    }
                }

                static Description color(std::string name, VkFormat fmt = VK_FORMAT_R16G16B16A16_SFLOAT) {
                    return {
                        .name = std::move(name),
                        .type = Type::ATTACHMENT_COLOR,
                        .format = fmt,
                        // Color attachments are frequently post-processed or resolved via blits/copies.
                        // Include transfer usage so stages like OutputPass can vkCmdBlitImage from them.
                        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                     VK_IMAGE_USAGE_SAMPLED_BIT |
                                     VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                     VK_IMAGE_USAGE_TRANSFER_DST_BIT
                    };
                }

                static Description depth(std::string name, VkFormat fmt = VK_FORMAT_D32_SFLOAT_S8_UINT) {
                    return {
                        .name = std::move(name),
                        .type = Type::ATTACHMENT_DEPTH,
                        .format = fmt,
                        .imageUsage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                     VK_IMAGE_USAGE_SAMPLED_BIT |
                                     VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                     VK_IMAGE_USAGE_TRANSFER_DST_BIT
                    };
                }

                static Description vertexBuffer(std::string name, VkDeviceSize size) {
                    return {
                        .name = std::move(name),
                        .type = Type::BUFFER_VERTEX,
                        .bufferUsage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                        .size = size
                    };
                }

                static Description indexBuffer(std::string name, VkDeviceSize size) {
                    return {
                        .name = std::move(name),
                        .type = Type::BUFFER_INDEX,
                        .bufferUsage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                        .size = size
                    };
                }

                constexpr static Description storageBuffer(std::string name, VkDeviceSize size) {
                    return {
                        .name = std::move(name),
                        .type = Type::BUFFER_STORAGE,
                        .bufferUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                        .size = size
                    };
                }
            };

            explicit Resource(const Type type, GPUImage &&image) : type_(type), data_(std::move(image)) {}
            explicit Resource(const Type type, Buffer &&buffer) : type_(type), data_(std::move(buffer)) {}

            Kind kind() const { return std::holds_alternative<GPUImage>(data_) ? Kind::IMAGE : Kind::BUFFER; }
            Type type() const {return type_;}

            GPUImage &asImage() { return std::get<GPUImage>(data_); }
            const GPUImage &asImage() const { return std::get<GPUImage>(data_); }

            Buffer &asBuffer() { return std::get<Buffer>(data_); }
            const Buffer &asBuffer() const { return std::get<Buffer>(data_); }

        private:
            Type type_{Type::INVALID};
            std::variant<GPUImage, Buffer> data_;
        };

        virtual ~IRenderStage() = default;

        virtual void getDeclaredOutputs(std::vector<Resource::Description> &out) const = 0;
        virtual void getDeclaredInputs(std::vector<std::string> &out) const = 0;

        virtual void onResourcesCreated(const std::unordered_map<std::string, std::reference_wrapper<Resource> > &resources) = 0;

        virtual void onSceneChanged(entt::registry &registry) {
        }

        virtual void record(VkCommandBuffer cmd, VkDescriptorSet globalSet) = 0;
    };
} // Atlas
