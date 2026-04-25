#pragma once


#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include "renderer/Device.hpp"
#include "renderer/abstraction/GPUBuffer.hpp"
#include "asset/Asset.hpp"

namespace Atlas {
    class Mesh final : public Asset {
    public:
        struct Vertex {
            glm::vec3 position{};
            glm::vec3 color{};
            glm::vec3 normal{};
            glm::vec2 uv{};
            glm::vec4 tangent{};

            static std::vector<VkVertexInputBindingDescription> getBindingDescriptions();
            static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions();

            bool operator==(const Vertex& other) const {
                return position == other.position && color == other.color && normal == other.normal && uv == other.uv;
            }
        };

        struct Builder {
            std::vector<Vertex> vertices{};
            std::vector<uint32_t> indices{};

            void loadModel(const std::string& filepath);
        };
        Mesh(Device &device, const Builder &builder);
        ~Mesh() override = default;

        Mesh(const Mesh &) = delete;
        Mesh &operator=(const Mesh &) = delete;

        void bind(VkCommandBuffer commandBuffer);
        void draw(VkCommandBuffer commandBuffer);

        const std::vector<Vertex>& getVertices() const { return vertices_; }
        const std::vector<uint32_t>& getIndices() const { return indices_; }

        static std::unique_ptr<Mesh> createSphere(Device& device, float radius = 1.0f, uint32_t segments = 32, uint32_t rings = 16);
        static std::unique_ptr<Mesh> createCube(Device &device, float size = 1.0f);
        static std::unique_ptr<Mesh> createPlane(Device &device, float width, float height);

        static size_t computeHash(const std::vector<Vertex> &vertices, const std::vector<uint32_t> &indices);
    private:

        void createVertexBuffers(const std::vector<Vertex> &vertices);
        void createIndexBuffers(const std::vector<uint32_t> &indices);

        Device &device;

        std::vector<Vertex> vertices_;
        std::vector<uint32_t> indices_;

        std::unique_ptr<GPUBuffer> vertexBuffer;
        uint32_t vertexCount;

        std::unique_ptr<GPUBuffer> indexBuffer;
        uint32_t indexCount;
        bool hasIndexBuffer = false;
    };
}
