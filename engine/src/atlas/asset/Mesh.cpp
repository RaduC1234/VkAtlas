#include "Mesh.hpp"

namespace Atlas {
    Mesh::Mesh(const std::vector<Vertex> &vertices, const std::vector<uint32_t> &indices)
        : IAsset(computeHash(vertices, indices)), vertices_(vertices), indices_(indices) {
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
