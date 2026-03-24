#include "Mesh.hpp"
#include "utils/Hash.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

#include <cassert>
#include <stdexcept>
#include <tiny_gltf.h>

namespace std {
    template<>
    struct hash<Atlas::Mesh::Vertex> {
        size_t operator()(const Atlas::Mesh::Vertex &vertex) const noexcept {
            size_t seed = 0;
            Atlas::hash(seed, vertex.position, vertex.color, vertex.normal, vertex.uv);
            return seed;
        }
    };

    template<>
    struct hash<Atlas::Mesh> {
        size_t operator()(const Atlas::Mesh &obj) const noexcept {
            return obj.getHash();
        }
    };
}

namespace Atlas {
    Mesh::Mesh(Device &device, const Builder &builder) : device{device} {
        vertices_ = builder.vertices;
        indices_ = builder.indices;
        createVertexBuffers(builder.vertices);
        createIndexBuffers(builder.indices);
    }

    void Mesh::bind(VkCommandBuffer commandBuffer) {
        VkBuffer buffers[] = {vertexBuffer->get()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, buffers, offsets);

        if (hasIndexBuffer) {
            vkCmdBindIndexBuffer(commandBuffer, indexBuffer->get(), 0, VK_INDEX_TYPE_UINT32);
        }
    }

    void Mesh::draw(VkCommandBuffer commandBuffer) {
        if (hasIndexBuffer) {
            vkCmdDrawIndexed(commandBuffer, indexCount, 1, 0, 0, 0);
        } else {
            vkCmdDraw(commandBuffer, vertexCount, 1, 0, 0);
        }
    }

    void Mesh::createVertexBuffers(const std::vector<Vertex> &vertices) {
        vertexCount = static_cast<uint32_t>(vertices.size());

        assert(vertexCount >= 3 && "Vertex count must be at least 3");
        VkDeviceSize bufferSize = sizeof(vertices[0]) * vertexCount;

        Buffer stagingBuffer(
            device,
            bufferSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
        );

        stagingBuffer.uploadData(vertices.data(), bufferSize);

        vertexBuffer = std::make_unique<Buffer>(
            device,
            bufferSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
        );

        Buffer::copy(device, stagingBuffer.get(), vertexBuffer->get(), bufferSize);
    }

    void Mesh::createIndexBuffers(const std::vector<uint32_t> &indices) {
        indexCount = static_cast<uint32_t>(indices.size());
        hasIndexBuffer = indexCount > 0;
        if (!hasIndexBuffer) return;

        VkDeviceSize bufferSize = sizeof(uint32_t) * indexCount;

        Buffer stagingBuffer(
            device,
            bufferSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
            VMA_ALLOCATION_CREATE_MAPPED_BIT
        );

        stagingBuffer.uploadData(indices.data(), bufferSize);

        indexBuffer = std::make_unique<Buffer>(
            device,
            bufferSize,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
        );

        Buffer::copy(device, stagingBuffer.get(), indexBuffer->get(), bufferSize);
    }

    // vertex
    std::vector<VkVertexInputBindingDescription> Mesh::Vertex::getBindingDescriptions() {
        std::vector<VkVertexInputBindingDescription> bindingDescriptions(1);
        bindingDescriptions[0].binding = 0;
        bindingDescriptions[0].stride = sizeof(Vertex);
        bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        return bindingDescriptions;
    }

    std::vector<VkVertexInputAttributeDescription> Mesh::Vertex::getAttributeDescriptions() {
        std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
        attributeDescriptions.push_back({0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)});
        attributeDescriptions.push_back({1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)});
        attributeDescriptions.push_back({2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)});
        attributeDescriptions.push_back({3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)});
        attributeDescriptions.push_back({4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, tangent)});

        return attributeDescriptions;
    }

    std::unique_ptr<Mesh> Mesh::createSphere(Device &device, float radius, uint32_t segments, uint32_t rings) {
        Mesh::Builder builder;

        for (uint32_t ring = 0; ring <= rings; ++ring) {
            float theta = static_cast<float>(ring) * glm::pi<float>() / static_cast<float>(rings);
            float sinTheta = std::sin(theta);
            float cosTheta = std::cos(theta);

            for (uint32_t segment = 0; segment <= segments; ++segment) {
                float phi = static_cast<float>(segment) * 2.0f * glm::pi<float>() / static_cast<float>(segments);
                float sinPhi = std::sin(phi);
                float cosPhi = std::cos(phi);

                Mesh::Vertex vertex{};

                vertex.position.x = radius * sinTheta * cosPhi;
                vertex.position.y = radius * cosTheta;
                vertex.position.z = radius * sinTheta * sinPhi;

                vertex.normal = glm::normalize(vertex.position);

                vertex.uv = glm::vec2(
                    static_cast<float>(segment) / static_cast<float>(segments),
                    static_cast<float>(ring) / static_cast<float>(rings)
                );

                vertex.color = glm::vec3(1.0f);
                vertex.tangent = glm::normalize(glm::vec4(-sinPhi, 0.0f, cosPhi, 1.0f));

                builder.vertices.push_back(vertex);
            }
        }

        for (uint32_t ring = 0; ring < rings; ++ring) {
            for (uint32_t segment = 0; segment < segments; ++segment) {
                uint32_t first = ring * (segments + 1) + segment;
                uint32_t second = first + segments + 1;

                builder.indices.push_back(first);
                builder.indices.push_back(second);
                builder.indices.push_back(first + 1);

                builder.indices.push_back(second);
                builder.indices.push_back(second + 1);
                builder.indices.push_back(first + 1);
            }
        }

        return std::make_unique<Mesh>(device, builder);
    }

    std::unique_ptr<Mesh> Mesh::createCube(Device &device, float size) {
        Mesh::Builder builder;
        float h = size / 2.0f;

        // Front face (+Z) - tangent = +X
        builder.vertices.push_back({{-h, -h, h}, {1, 1, 1}, {0, 0, 1}, {0.0f, 0.0f}, {1, 0, 0, 1}});
        builder.vertices.push_back({{h, -h, h}, {1, 1, 1}, {0, 0, 1}, {1.0f, 0.0f}, {1, 0, 0, 1}});
        builder.vertices.push_back({{h, h, h}, {1, 1, 1}, {0, 0, 1}, {1.0f, 1.0f}, {1, 0, 0, 1}});
        builder.vertices.push_back({{-h, h, h}, {1, 1, 1}, {0, 0, 1}, {0.0f, 1.0f}, {1, 0, 0, 1}});

        // Back face (-Z) - tangent = -X
        builder.vertices.push_back({{h, -h, -h}, {1, 1, 1}, {0, 0, -1}, {0.0f, 0.0f}, {-1, 0, 0, 1}});
        builder.vertices.push_back({{-h, -h, -h}, {1, 1, 1}, {0, 0, -1}, {1.0f, 0.0f}, {-1, 0, 0, 1}});
        builder.vertices.push_back({{-h, h, -h}, {1, 1, 1}, {0, 0, -1}, {1.0f, 1.0f}, {-1, 0, 0, 1}});
        builder.vertices.push_back({{h, h, -h}, {1, 1, 1}, {0, 0, -1}, {0.0f, 1.0f}, {-1, 0, 0, 1}});

        // Left face (-X) - tangent = +Z
        builder.vertices.push_back({{-h, -h, -h}, {1, 1, 1}, {-1, 0, 0}, {0.0f, 0.0f}, {0, 0, 1, 1}});
        builder.vertices.push_back({{-h, -h, h}, {1, 1, 1}, {-1, 0, 0}, {1.0f, 0.0f}, {0, 0, 1, 1}});
        builder.vertices.push_back({{-h, h, h}, {1, 1, 1}, {-1, 0, 0}, {1.0f, 1.0f}, {0, 0, 1, 1}});
        builder.vertices.push_back({{-h, h, -h}, {1, 1, 1}, {-1, 0, 0}, {0.0f, 1.0f}, {0, 0, 1, 1}});

        // Right face (+X) - tangent = -Z
        builder.vertices.push_back({{h, -h, h}, {1, 1, 1}, {1, 0, 0}, {0.0f, 0.0f}, {0, 0, -1, 1}});
        builder.vertices.push_back({{h, -h, -h}, {1, 1, 1}, {1, 0, 0}, {1.0f, 0.0f}, {0, 0, -1, 1}});
        builder.vertices.push_back({{h, h, -h}, {1, 1, 1}, {1, 0, 0}, {1.0f, 1.0f}, {0, 0, -1, 1}});
        builder.vertices.push_back({{h, h, h}, {1, 1, 1}, {1, 0, 0}, {0.0f, 1.0f}, {0, 0, -1, 1}});

        // Top face (+Y) - tangent = +X
        builder.vertices.push_back({{-h, h, h}, {1, 1, 1}, {0, 1, 0}, {0.0f, 0.0f}, {1, 0, 0, 1}});
        builder.vertices.push_back({{h, h, h}, {1, 1, 1}, {0, 1, 0}, {1.0f, 0.0f}, {1, 0, 0, 1}});
        builder.vertices.push_back({{h, h, -h}, {1, 1, 1}, {0, 1, 0}, {1.0f, 1.0f}, {1, 0, 0, 1}});
        builder.vertices.push_back({{-h, h, -h}, {1, 1, 1}, {0, 1, 0}, {0.0f, 1.0f}, {1, 0, 0, 1}});

        // Bottom face (-Y) - tangent = +X
        builder.vertices.push_back({{-h, -h, -h}, {1, 1, 1}, {0, -1, 0}, {0.0f, 0.0f}, {1, 0, 0, 1}});
        builder.vertices.push_back({{h, -h, -h}, {1, 1, 1}, {0, -1, 0}, {1.0f, 0.0f}, {1, 0, 0, 1}});
        builder.vertices.push_back({{h, -h, h}, {1, 1, 1}, {0, -1, 0}, {1.0f, 1.0f}, {1, 0, 0, 1}});
        builder.vertices.push_back({{-h, -h, h}, {1, 1, 1}, {0, -1, 0}, {0.0f, 1.0f}, {1, 0, 0, 1}});

        // Indices for all 6 faces (2 triangles per face)
        for (uint32_t i = 0; i < 6; ++i) {
            uint32_t base = i * 4;
            builder.indices.push_back(base);
            builder.indices.push_back(base + 1);
            builder.indices.push_back(base + 2);

            builder.indices.push_back(base);
            builder.indices.push_back(base + 2);
            builder.indices.push_back(base + 3);
        }

        return std::make_unique<Mesh>(device, builder);
    }

    std::unique_ptr<Mesh> Mesh::createPlane(Device &device, float width, float height) {
        Mesh::Builder builder;

        float halfWidth = width / 2.0f;
        float halfHeight = height / 2.0f;

        builder.vertices.push_back({{-halfWidth, 0.0f, -halfHeight}, {1, 1, 1}, {0, -1, 0}, {0.0f, 0.0f}, {1, 0, 0, 1}});
        builder.vertices.push_back({{halfWidth, 0.0f, -halfHeight}, {1, 1, 1}, {0, -1, 0}, {1.0f, 0.0f}, {1, 0, 0, 1}});
        builder.vertices.push_back({{halfWidth, 0.0f, halfHeight}, {1, 1, 1}, {0, -1, 0}, {1.0f, 1.0f}, {1, 0, 0, 1}});
        builder.vertices.push_back({{-halfWidth, 0.0f, halfHeight}, {1, 1, 1}, {0, -1, 0}, {0.0f, 1.0f}, {1, 0, 0, 1}});


        builder.indices.push_back(0);
        builder.indices.push_back(1);
        builder.indices.push_back(2);

        builder.indices.push_back(0);
        builder.indices.push_back(2);
        builder.indices.push_back(3);

        return std::make_unique<Mesh>(device, builder);
    }

    size_t Mesh::computeHash(const std::vector<Vertex> &vertices, const std::vector<uint32_t> &indices) {
        size_t vertexHash = 0;
        Atlas::hash(vertexHash, vertices.size());

        for (const auto &vertex: vertices) {
            Atlas::hash(vertexHash, vertex.position, vertex.color, vertex.normal, vertex.uv, vertex.tangent);
        }

        size_t indexHash = 0;
        Atlas::hash(indexHash, indices.size());

        for (const auto &index: indices) {
            Atlas::hash(indexHash, index);
        }

        size_t finalSeed = 0;
        Atlas::hash(finalSeed, vertexHash, indexHash);
        return finalSeed;
    }
}
