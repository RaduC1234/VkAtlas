#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <glm/glm.hpp>

#include "IAsset.hpp"

namespace Atlas {
    class Mesh : public IAsset {
    public:
        struct Vertex {
            glm::vec3 position{};
            glm::vec3 color{};
            glm::vec3 normal{};
            glm::vec2 uv{};
            glm::vec4 tangent{};

            bool operator==(const Vertex &other) const {
                return position == other.position
                       && color == other.color
                       && normal == other.normal
                       && uv == other.uv
                       && tangent == other.tangent;
            }
        };

        Mesh(const std::vector<Vertex> &vertices, const std::vector<uint32_t> &indices);

        const std::vector<Vertex> &vertices() const { return vertices_; }
        const std::vector<uint32_t> &indices() const { return indices_; }

        static std::shared_ptr<Mesh> fromFile(const std::string &path);
        static void saveFile(const Mesh &mesh, const std::string &path);

    private:
        static uint64_t computeHash(const std::vector<Vertex> &vertices, const std::vector<uint32_t> &indices);

        std::vector<Vertex> vertices_;
        std::vector<uint32_t> indices_;
    };
}
