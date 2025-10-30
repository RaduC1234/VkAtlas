#include "Mesh.hpp"
#include "utils/Utils.hpp"

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

#include <cassert>

namespace std {
    template<>
    struct hash<Atlas::Mesh::Vertex> {
        size_t operator()(const Atlas::Mesh::Vertex &vertex) const noexcept {
            size_t seed = 0;
            Atlas::hash(seed, vertex.position, vertex.color, vertex.normal, vertex.uv);
            return seed;
        }
    };
}

namespace Atlas {
    Mesh::Mesh(Device &device, const Builder &builder) : device{device} {
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
        // UV is vec2 in the vertex struct; use R32G32_SFLOAT
        attributeDescriptions.push_back({3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)});

        return attributeDescriptions;
    }

    void Mesh::Builder::loadModel(const std::string &filepath) {

    }

    std::unique_ptr<Mesh> Mesh::createModelFromFileObj(Device &device, const std::string &filepath) {
        Builder builder{};
        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string warn, err;

        if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filepath.c_str())) {
            throw std::runtime_error(warn + err);
        }

        builder.vertices.clear();
        builder.indices.clear();

        std::unordered_map<Vertex, uint32_t> uniqueVertices;
        for (const auto &shape: shapes) {
            for (const auto &index: shape.mesh.indices) {
                Vertex vertex{};

                if (index.vertex_index >= 0) {
                    vertex.position = glm::vec3(
                        attrib.vertices[3 * index.vertex_index + 0],
                        attrib.vertices[3 * index.vertex_index + 1],
                        attrib.vertices[3 * index.vertex_index + 2]
                    );

                    if (!attrib.colors.empty()) {
                        vertex.color = glm::vec3(
                            attrib.colors[3 * index.vertex_index + 0],
                            attrib.colors[3 * index.vertex_index + 1],
                            attrib.colors[3 * index.vertex_index + 2]
                        );
                    } else {
                        vertex.color = glm::vec3(1.0f);
                    }
                }

                if (index.normal_index >= 0 && !attrib.normals.empty()) {
                    vertex.normal = glm::vec3(
                        attrib.normals[3 * index.normal_index + 0],
                        attrib.normals[3 * index.normal_index + 1],
                        attrib.normals[3 * index.normal_index + 2]
                    );
                } else {
                    vertex.normal = glm::vec3(0.0f);
                }

                if (index.texcoord_index >= 0 && !attrib.texcoords.empty()) {
                    vertex.uv = glm::vec2(
                        attrib.texcoords[2 * index.texcoord_index + 0],
                        attrib.texcoords[2 * index.texcoord_index + 1]
                    );
                } else {
                    vertex.uv = glm::vec2(0.0f);
                }

                if (uniqueVertices.count(vertex) == 0) {
                    uniqueVertices[vertex] = static_cast<uint32_t>(builder.vertices.size());
                    builder.vertices.push_back(vertex);
                }
                builder.indices.push_back(uniqueVertices[vertex]);
            }
        }

        return std::make_unique<Mesh>(device, builder);
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

        // Front face
        builder.vertices.push_back({{-h, -h, h}, {1, 1, 1}, {0, 0, 1}, {0.0f, 0.0f}});
        builder.vertices.push_back({{h, -h, h}, {1, 1, 1}, {0, 0, 1}, {1.0f, 0.0f}});
        builder.vertices.push_back({{h, h, h}, {1, 1, 1}, {0, 0, 1}, {1.0f, 1.0f}});
        builder.vertices.push_back({{-h, h, h}, {1, 1, 1}, {0, 0, 1}, {0.0f, 1.0f}});

        // Back face
        builder.vertices.push_back({{h, -h, -h}, {1, 1, 1}, {0, 0, -1}, {0.0f, 0.0f}});
        builder.vertices.push_back({{-h, -h, -h}, {1, 1, 1}, {0, 0, -1}, {1.0f, 0.0f}});
        builder.vertices.push_back({{-h, h, -h}, {1, 1, 1}, {0, 0, -1}, {1.0f, 1.0f}});
        builder.vertices.push_back({{h, h, -h}, {1, 1, 1}, {0, 0, -1}, {0.0f, 1.0f}});

        // Top face
        builder.vertices.push_back({{-h, h, h}, {1, 1, 1}, {0, 1, 0}, {0.0f, 0.0f}});
        builder.vertices.push_back({{h, h, h}, {1, 1, 1}, {0, 1, 0}, {1.0f, 0.0f}});
        builder.vertices.push_back({{h, h, -h}, {1, 1, 1}, {0, 1, 0}, {1.0f, 1.0f}});
        builder.vertices.push_back({{-h, h, -h}, {1, 1, 1}, {0, 1, 0}, {0.0f, 1.0f}});

        // Bottom face
        builder.vertices.push_back({{-h, -h, -h}, {1, 1, 1}, {0, -1, 0}, {0.0f, 0.0f}});
        builder.vertices.push_back({{h, -h, -h}, {1, 1, 1}, {0, -1, 0}, {1.0f, 0.0f}});
        builder.vertices.push_back({{h, -h, h}, {1, 1, 1}, {0, -1, 0}, {1.0f, 1.0f}});
        builder.vertices.push_back({{-h, -h, h}, {1, 1, 1}, {0, -1, 0}, {0.0f, 1.0f}});

        // Right face
        builder.vertices.push_back({{h, -h, h}, {1, 1, 1}, {1, 0, 0}, {0.0f, 0.0f}});
        builder.vertices.push_back({{h, -h, -h}, {1, 1, 1}, {1, 0, 0}, {1.0f, 0.0f}});
        builder.vertices.push_back({{h, h, -h}, {1, 1, 1}, {1, 0, 0}, {1.0f, 1.0f}});
        builder.vertices.push_back({{h, h, h}, {1, 1, 1}, {1, 0, 0}, {0.0f, 1.0f}});

        // Left face
        builder.vertices.push_back({{-h, -h, -h}, {1, 1, 1}, {-1, 0, 0}, {0.0f, 0.0f}});
        builder.vertices.push_back({{-h, -h, h}, {1, 1, 1}, {-1, 0, 0}, {1.0f, 0.0f}});
        builder.vertices.push_back({{-h, h, h}, {1, 1, 1}, {-1, 0, 0}, {1.0f, 1.0f}});
        builder.vertices.push_back({{-h, h, -h}, {1, 1, 1}, {-1, 0, 0}, {0.0f, 1.0f}});

        // Indices (6 faces * 2 triangles * 3 vertices)
        uint32_t indices[] = {
            0, 1, 2, 2, 3, 0, // Front
            4, 5, 6, 6, 7, 4, // Back
            8, 9, 10, 10, 11, 8, // Top
            12, 13, 14, 14, 15, 12, // Bottom
            16, 17, 18, 18, 19, 16, // Right
            20, 21, 22, 22, 23, 20 // Left
        };

        for (uint32_t idx: indices) {
            builder.indices.push_back(idx);
        }

        return std::make_unique<Mesh>(device, builder);
    }
}
