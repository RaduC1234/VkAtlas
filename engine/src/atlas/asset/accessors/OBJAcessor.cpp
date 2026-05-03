#include "OBJAcessor.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <filesystem>
#include <tiny_obj_loader.h>
#include <glm/gtx/hash.hpp>

#include "core/Log.hpp"
#include "entity/Object.hpp"

namespace Atlas {
    namespace {
        struct VertexHasher {
            size_t operator()(const Mesh::Vertex &v) const noexcept {
                size_t seed = 0;
                std::hash<glm::vec3> h3;
                std::hash<glm::vec2> h2;
                seed ^= h3(v.position) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
                seed ^= h3(v.color)    + 0x9e3779b9 + (seed << 6) + (seed >> 2);
                seed ^= h3(v.normal)   + 0x9e3779b9 + (seed << 6) + (seed >> 2);
                seed ^= h2(v.uv)       + 0x9e3779b9 + (seed << 6) + (seed >> 2);
                return seed;
            }
        };
    }

    OBJAccessor::OBJAccessor(ExecutorService &service) : executor(service) {}

    std::vector<entt::entity> OBJAccessor::importAsset(
        const std::string &path,
        entt::registry &registry,
        entt::entity parentEntity) {

        std::filesystem::path fullPath = AssetManager::get().rootPath() / path;
        std::string mtlBaseDir = fullPath.parent_path().string();

        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string warn, err;

        if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err,
                              fullPath.string().c_str(), mtlBaseDir.c_str())) {
            AT_ERROR("OBJAccessor: failed to load {}: {}", path, warn + err);
            return {};
        }
        if (!warn.empty())
            AT_WARN("OBJAccessor: {}: {}", path, warn);

        AT_INFO("OBJAccessor: loading {} ({} shapes, {} materials)", path, shapes.size(), materials.size());

        std::filesystem::path virtualDir = std::filesystem::path(path).parent_path();

        std::vector<AssetHandle> albedoHandles(materials.size(), INVALID_ASSET_HANDLE);
        std::vector<AssetHandle> normalHandles(materials.size(), INVALID_ASSET_HANDLE);

        for (size_t matIdx = 0; matIdx < materials.size(); ++matIdx) {
            const auto &mat = materials[matIdx];
            if (!mat.diffuse_texname.empty()) {
                std::string texPath = (virtualDir / mat.diffuse_texname).generic_string();
                albedoHandles[matIdx] = AssetManager::get().loadTexture(texPath, VK_FORMAT_R8G8B8A8_SRGB);
            }
            const std::string &normalName = !mat.normal_texname.empty() ? mat.normal_texname : mat.bump_texname;
            if (!normalName.empty()) {
                std::string texPath = (virtualDir / normalName).generic_string();
                normalHandles[matIdx] = AssetManager::get().loadTexture(texPath, VK_FORMAT_R8G8B8A8_UNORM);
            }
        }

        std::vector<entt::entity> entities;

        for (size_t shapeIdx = 0; shapeIdx < shapes.size(); ++shapeIdx) {
            const auto &shape = shapes[shapeIdx];

            std::unordered_map<Mesh::Vertex, uint32_t, VertexHasher> uniqueVertices;
            std::vector<Mesh::Vertex> vertices;
            std::vector<uint32_t> indices;

            for (const auto &index : shape.mesh.indices) {
                Mesh::Vertex vertex{};

                if (index.vertex_index >= 0) {
                    vertex.position = glm::vec3(
                        attrib.vertices[3 * index.vertex_index + 0],
                        attrib.vertices[3 * index.vertex_index + 1],
                        attrib.vertices[3 * index.vertex_index + 2]
                    );
                    vertex.color = attrib.colors.empty() ? glm::vec3(1.0f) : glm::vec3(
                        attrib.colors[3 * index.vertex_index + 0],
                        attrib.colors[3 * index.vertex_index + 1],
                        attrib.colors[3 * index.vertex_index + 2]
                    );
                }

                if (index.normal_index >= 0 && !attrib.normals.empty()) {
                    vertex.normal = glm::vec3(
                        attrib.normals[3 * index.normal_index + 0],
                        attrib.normals[3 * index.normal_index + 1],
                        attrib.normals[3 * index.normal_index + 2]
                    );
                }

                if (index.texcoord_index >= 0 && !attrib.texcoords.empty()) {
                    vertex.uv = glm::vec2(
                        attrib.texcoords[2 * index.texcoord_index + 0],
                        attrib.texcoords[2 * index.texcoord_index + 1]
                    );
                }

                vertex.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);

                if (!uniqueVertices.count(vertex)) {
                    uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
                    vertices.push_back(vertex);
                }
                indices.push_back(uniqueVertices[vertex]);
            }

            if (vertices.empty()) continue;

            std::string meshPath = path + "#shape" + std::to_string(shapeIdx);
            AssetHandle meshHandle = AssetManager::get().getOrCreateMesh(vertices, indices, meshPath);

            int matId = shape.mesh.material_ids.empty() ? -1 : shape.mesh.material_ids[0];

            auto entity = registry.create();

            auto &sn  = registry.emplace<SceneNodeComponent>(entity);
            sn.name   = shape.name.empty() ? ("Shape_" + std::to_string(shapeIdx)) : shape.name;
            sn.parent = parentEntity;

            auto &transform       = registry.emplace<TransformComponent>(entity);
            transform.translation = glm::vec3(0.0f);
            transform.rotation    = glm::vec3(0.0f);
            transform.scale       = glm::vec3(1.0f);

            auto &modelComp      = registry.emplace<ModelComponent>(entity);
            modelComp.meshHandle = meshHandle;

            auto &material = registry.emplace<MaterialComponent>(entity);
            if (matId >= 0 && matId < static_cast<int>(materials.size())) {
                const auto &mat = materials[matId];
                material.baseColor     = glm::vec4(mat.diffuse[0], mat.diffuse[1], mat.diffuse[2], mat.dissolve);
                material.albedoTexture = albedoHandles[matId];
                material.normalMap     = normalHandles[matId];
            } else {
                material.baseColor = glm::vec4(1.0f);
            }

            entities.push_back(entity);
        }

        AT_INFO("OBJAccessor: created {} entities from {}", entities.size(), path);
        return entities;
    }

    std::vector<std::byte> OBJAccessor::exportAsset(
        const std::vector<entt::entity> &,
        const entt::registry &) {
        return {};
    }
}