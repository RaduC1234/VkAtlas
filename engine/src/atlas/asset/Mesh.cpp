#include "Mesh.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace Atlas {
    namespace MeshFileFormat {
        constexpr char magic[] = {'A', 'T', 'L', 'M', 'E', 'S', 'H', '\0'};
        constexpr uint32_t version = 1;

        struct Header {
            char magic[8]{};
            uint32_t version = MeshFileFormat::version;
            uint32_t vertexStride = sizeof(Mesh::Vertex);
            uint64_t vertexCount = 0;
            uint64_t indexCount = 0;
        };

        bool hasValidMagic(const Header &header) {
            return std::memcmp(header.magic, magic, sizeof(header.magic)) == 0;
        }
    }

    Mesh::Mesh(const std::vector<Vertex> &vertices, const std::vector<uint32_t> &indices)
        : IAsset(computeHash(vertices, indices)), vertices_(vertices), indices_(indices) {
    }

    std::shared_ptr<Mesh> Mesh::fromFile(const std::string &path) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open mesh file: " + path);
        }

        MeshFileFormat::Header header{};
        file.read(reinterpret_cast<char *>(&header), sizeof(header));
        if (!file || !MeshFileFormat::hasValidMagic(header)) {
            throw std::runtime_error("Invalid Atlas mesh file: " + path);
        }

        if (header.version != MeshFileFormat::version) {
            throw std::runtime_error("Unsupported Atlas mesh version in: " + path);
        }

        if (header.vertexStride != sizeof(Vertex)) {
            throw std::runtime_error("Unsupported Atlas mesh vertex stride in: " + path);
        }

        std::vector<Vertex> vertices(static_cast<size_t>(header.vertexCount));
        std::vector<uint32_t> indices(static_cast<size_t>(header.indexCount));

        if (!vertices.empty()) {
            file.read(reinterpret_cast<char *>(vertices.data()), static_cast<std::streamsize>(vertices.size() * sizeof(Vertex)));
        }

        if (!indices.empty()) {
            file.read(reinterpret_cast<char *>(indices.data()), static_cast<std::streamsize>(indices.size() * sizeof(uint32_t)));
        }

        if (!file.good() && !file.eof()) {
            throw std::runtime_error("Failed to read Atlas mesh file: " + path);
        }

        return std::make_shared<Mesh>(vertices, indices);
    }

    void Mesh::saveFile(const Mesh &mesh, const std::string &path) {
        const std::filesystem::path filePath(path);
        if (filePath.has_parent_path()) {
            std::filesystem::create_directories(filePath.parent_path());
        }

        std::ofstream file(filePath, std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to create mesh file: " + path);
        }

        MeshFileFormat::Header header{};
        std::memcpy(header.magic, MeshFileFormat::magic, sizeof(header.magic));
        header.vertexCount = static_cast<uint64_t>(mesh.vertices().size());
        header.indexCount = static_cast<uint64_t>(mesh.indices().size());

        file.write(reinterpret_cast<const char *>(&header), sizeof(header));
        if (!mesh.vertices().empty()) {
            file.write(reinterpret_cast<const char *>(mesh.vertices().data()), static_cast<std::streamsize>(mesh.vertices().size() * sizeof(Vertex)));
        }
        if (!mesh.indices().empty()) {
            file.write(reinterpret_cast<const char *>(mesh.indices().data()), static_cast<std::streamsize>(mesh.indices().size() * sizeof(uint32_t)));
        }

        if (!file.good()) {
            throw std::runtime_error("Failed to write Atlas mesh file: " + path);
        }
    }

    uint64_t Mesh::computeHash(const std::vector<Vertex> &vertices, const std::vector<uint32_t> &indices) {
        constexpr uint64_t FNV_OFFSET_BASIS = 14695981039346656037ull;
        constexpr uint64_t FNV_PRIME = 1099511628211ull;

        uint64_t hash = FNV_OFFSET_BASIS;

        auto hashBytes = [&](const void *data, size_t size) {
            const auto *bytes = static_cast<const std::byte *>(data);
            for (size_t i = 0; i < size; ++i) {
                hash ^= std::to_integer<uint8_t>(bytes[i]);
                hash *= FNV_PRIME;
            }
        };

        const uint64_t vertexCount = vertices.size();
        const uint64_t indexCount = indices.size();
        hashBytes(&vertexCount, sizeof(vertexCount));
        hashBytes(&indexCount, sizeof(indexCount));

        if (!vertices.empty())
            hashBytes(vertices.data(), vertices.size() * sizeof(Vertex));

        if (!indices.empty())
            hashBytes(indices.data(), indices.size() * sizeof(uint32_t));

        return hash;
    }
}
