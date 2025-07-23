#pragma once
#include "Device.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include "Buffer.hpp"

namespace Atlas {
    class Mesh {
    public:
        struct Vertex {
            glm::vec3 position{};
            glm::vec3 color{};
            glm::vec3 normal{};
            glm::vec2 uv{};

            static std::vector<VkVertexInputBindingDescription> getBindingDescriptions();
            static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions();
        };

        struct Builder {
            std::vector<Vertex> vertices{};
            std::vector<uint32_t> indices{};

            void loadModel(const std::string& filepath);
        };

        Mesh(Device &device, const Builder &builder);
        ~Mesh() = default;

        Mesh(const Mesh &) = delete;
        Mesh &operator=(const Mesh &) = delete;

        void bind(VkCommandBuffer commandBuffer);
        void draw(VkCommandBuffer commandBuffer);

        static std::unique_ptr<Mesh> createModelFromFileObj(Device &device, const std::string& filepath);

    private:
        void createVertexBuffers(const std::vector<Vertex> &vertices);
        void createIndexBuffers(const std::vector<uint32_t> &indices);

        Device &device;

        std::unique_ptr<Buffer> vertexBuffer;
        uint32_t vertexCount;

        std::unique_ptr<Buffer> indexBuffer;
        uint32_t indexCount;
        bool hasIndexBuffer = false;
    };
}
