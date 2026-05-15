#include "OBJAcessor.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <tiny_obj_loader.h>
#include <glm/gtx/hash.hpp>
#include <unordered_map>
#include <unordered_set>

#include "core/Log.hpp"
#include "entity/Object.hpp"

namespace Atlas {
    namespace {
        constexpr float EPSILON = 1.0e-8f;

        struct VertexHasher {
            size_t operator()(const Mesh::Vertex &v) const noexcept {
                size_t seed = 0;
                std::hash<glm::vec3> h3;
                std::hash<glm::vec2> h2;
                seed ^= h3(v.position) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
                seed ^= h3(v.color) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
                seed ^= h3(v.normal) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
                seed ^= h2(v.uv) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
                return seed;
            }
        };

        bool hasLength(const glm::vec3 &value) {
            return glm::dot(value, value) > EPSILON;
        }

        bool hasLength(const glm::vec2 &value) {
            return glm::dot(value, value) > EPSILON;
        }

        glm::vec3 fallbackNormal(const glm::vec3 &normal) {
            return hasLength(normal) ? glm::normalize(normal) : glm::vec3(0.0f, 1.0f, 0.0f);
        }

        glm::vec4 fallbackTangent(const glm::vec3 &normal) {
            const glm::vec3 n = fallbackNormal(normal);
            const glm::vec3 helper = std::abs(n.y) < 0.999f
                                         ? glm::vec3(0.0f, 1.0f, 0.0f)
                                         : glm::vec3(1.0f, 0.0f, 0.0f);
            return glm::vec4(glm::normalize(glm::cross(helper, n)), 1.0f);
        }

        void finalizeVertexFrames(std::vector<Mesh::Vertex> &vertices, const std::vector<uint32_t> &indices) {
            if (vertices.empty()) {
                return;
            }

            std::vector<glm::vec3> normalSums(vertices.size(), glm::vec3(0.0f));
            std::vector<glm::vec3> tangentSums(vertices.size(), glm::vec3(0.0f));
            std::vector<glm::vec3> bitangentSums(vertices.size(), glm::vec3(0.0f));

            for (size_t i = 0; i + 2 < indices.size(); i += 3) {
                const uint32_t i0 = indices[i + 0];
                const uint32_t i1 = indices[i + 1];
                const uint32_t i2 = indices[i + 2];

                if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) {
                    continue;
                }

                const glm::vec3 p0 = vertices[i0].position;
                const glm::vec3 p1 = vertices[i1].position;
                const glm::vec3 p2 = vertices[i2].position;
                const glm::vec3 edge1 = p1 - p0;
                const glm::vec3 edge2 = p2 - p0;

                const glm::vec3 faceNormal = glm::cross(edge1, edge2);
                if (hasLength(faceNormal)) {
                    const glm::vec3 n = glm::normalize(faceNormal);
                    normalSums[i0] += n;
                    normalSums[i1] += n;
                    normalSums[i2] += n;
                }

                const glm::vec2 uv0 = vertices[i0].uv;
                const glm::vec2 uv1 = vertices[i1].uv;
                const glm::vec2 uv2 = vertices[i2].uv;
                const glm::vec2 deltaUv1 = uv1 - uv0;
                const glm::vec2 deltaUv2 = uv2 - uv0;
                const float determinant = deltaUv1.x * deltaUv2.y - deltaUv1.y * deltaUv2.x;

                if (std::abs(determinant) > EPSILON && (hasLength(deltaUv1) || hasLength(deltaUv2))) {
                    const float invDet = 1.0f / determinant;
                    const glm::vec3 tangent = (edge1 * deltaUv2.y - edge2 * deltaUv1.y) * invDet;
                    const glm::vec3 bitangent = (edge2 * deltaUv1.x - edge1 * deltaUv2.x) * invDet;

                    tangentSums[i0] += tangent;
                    tangentSums[i1] += tangent;
                    tangentSums[i2] += tangent;
                    bitangentSums[i0] += bitangent;
                    bitangentSums[i1] += bitangent;
                    bitangentSums[i2] += bitangent;
                }
            }

            for (size_t i = 0; i < vertices.size(); ++i) {
                auto &vertex = vertices[i];

                if (!hasLength(vertex.normal)) {
                    vertex.normal = fallbackNormal(normalSums[i]);
                } else {
                    vertex.normal = glm::normalize(vertex.normal);
                }

                glm::vec3 tangent = tangentSums[i];
                if (hasLength(tangent)) {
                    tangent = tangent - vertex.normal * glm::dot(vertex.normal, tangent);
                    if (hasLength(tangent)) {
                        tangent = glm::normalize(tangent);
                        const float handedness = glm::dot(glm::cross(vertex.normal, tangent), bitangentSums[i]) < 0.0f ? -1.0f : 1.0f;
                        vertex.tangent = glm::vec4(tangent, handedness);
                        continue;
                    }
                }

                vertex.tangent = fallbackTangent(vertex.normal);
            }
        }

        Mesh::Vertex makeVertex(const tinyobj::attrib_t &attrib, const tinyobj::index_t &index) {
            Mesh::Vertex vertex{};

            if (index.vertex_index >= 0) {
                const size_t vertexOffset = static_cast<size_t>(index.vertex_index) * 3;
                if (vertexOffset + 2 < attrib.vertices.size()) {
                    vertex.position = glm::vec3(
                        attrib.vertices[vertexOffset + 0],
                        attrib.vertices[vertexOffset + 1],
                        attrib.vertices[vertexOffset + 2]
                    );
                }

                if (!attrib.colors.empty() && vertexOffset + 2 < attrib.colors.size()) {
                    vertex.color = glm::vec3(
                        attrib.colors[vertexOffset + 0],
                        attrib.colors[vertexOffset + 1],
                        attrib.colors[vertexOffset + 2]
                    );
                } else {
                    vertex.color = glm::vec3(1.0f);
                }
            }

            if (index.normal_index >= 0) {
                const size_t normalOffset = static_cast<size_t>(index.normal_index) * 3;
                if (normalOffset + 2 < attrib.normals.size()) {
                    vertex.normal = glm::vec3(
                        attrib.normals[normalOffset + 0],
                        attrib.normals[normalOffset + 1],
                        attrib.normals[normalOffset + 2]
                    );
                }
            }

            if (index.texcoord_index >= 0) {
                const size_t texcoordOffset = static_cast<size_t>(index.texcoord_index) * 2;
                if (texcoordOffset + 1 < attrib.texcoords.size()) {
                    vertex.uv = glm::vec2(
                        attrib.texcoords[texcoordOffset + 0],
                        attrib.texcoords[texcoordOffset + 1]
                    );
                }
            }

            vertex.tangent = fallbackTangent(vertex.normal);
            return vertex;
        }

        uint32_t appendVertex(
            const Mesh::Vertex &vertex,
            std::vector<Mesh::Vertex> &vertices,
            std::vector<uint32_t> &indices,
            std::unordered_map<Mesh::Vertex, uint32_t, VertexHasher> &uniqueVertices) {
            auto it = uniqueVertices.find(vertex);
            if (it == uniqueVertices.end()) {
                const uint32_t index = static_cast<uint32_t>(vertices.size());
                it = uniqueVertices.emplace(vertex, index).first;
                vertices.push_back(vertex);
            }

            indices.push_back(it->second);
            return it->second;
        }

        std::string sanitizeObjName(const std::string &name, const std::string &fallback) {
            const std::string &source = name.empty() ? fallback : name;
            std::string result;
            result.reserve(source.size());

            for (const char ch: source) {
                if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-' || ch == '.') {
                    result.push_back(ch);
                } else if (std::isspace(static_cast<unsigned char>(ch))) {
                    result.push_back('_');
                }
            }

            return result.empty() ? fallback : result;
        }

        void appendEntityAndChildren(
            entt::entity entity,
            const entt::registry &registry,
            std::vector<entt::entity> &outEntities,
            std::unordered_set<entt::id_type> &visited) {
            if (entity == entt::null || !registry.valid(entity)) {
                return;
            }

            const entt::id_type entityId = entt::to_integral(entity);
            if (!visited.insert(entityId).second) {
                return;
            }

            outEntities.push_back(entity);

            if (const auto *node = registry.try_get<SceneNodeComponent>(entity)) {
                for (const entt::entity child: node->children) {
                    appendEntityAndChildren(child, registry, outEntities, visited);
                }
            }
        }

        std::vector<std::byte> toBytes(const std::string &text) {
            std::vector<std::byte> bytes;
            bytes.reserve(text.size());
            for (const char ch: text) {
                bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
            }
            return bytes;
        }
    }

    OBJAccessor::OBJAccessor(AssetManager &assets, ExecutorService &service) : assets(assets), executor(service) {
    }

    std::vector<entt::entity> OBJAccessor::importAsset(
        const std::string &path,
        entt::registry &registry,
        entt::entity parentEntity) {
        std::filesystem::path fullPath = assets.rootPath() / path;
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
        std::vector<AssetHandle> metallicHandles(materials.size(), INVALID_ASSET_HANDLE);
        std::vector<AssetHandle> roughnessHandles(materials.size(), INVALID_ASSET_HANDLE);
        std::vector<AssetHandle> ambientHandles(materials.size(), INVALID_ASSET_HANDLE);

        for (size_t matIdx = 0; matIdx < materials.size(); ++matIdx) {
            const auto &mat = materials[matIdx];
            if (!mat.diffuse_texname.empty()) {
                std::string texPath = (virtualDir / mat.diffuse_texname).generic_string();
                albedoHandles[matIdx] = assets.loadTexture(texPath, VK_FORMAT_R8G8B8A8_SRGB);
            }
            const std::string &normalName = !mat.normal_texname.empty() ? mat.normal_texname : mat.bump_texname;
            if (!normalName.empty()) {
                std::string texPath = (virtualDir / normalName).generic_string();
                normalHandles[matIdx] = assets.loadTexture(texPath, VK_FORMAT_R8G8B8A8_UNORM);
            }
            if (!mat.metallic_texname.empty()) {
                std::string texPath = (virtualDir / mat.metallic_texname).generic_string();
                metallicHandles[matIdx] = assets.loadTexture(texPath, VK_FORMAT_R8G8B8A8_UNORM);
            }
            if (!mat.roughness_texname.empty()) {
                std::string texPath = (virtualDir / mat.roughness_texname).generic_string();
                roughnessHandles[matIdx] = assets.loadTexture(texPath, VK_FORMAT_R8G8B8A8_UNORM);
            }
            if (!mat.ambient_texname.empty()) {
                std::string texPath = (virtualDir / mat.ambient_texname).generic_string();
                ambientHandles[matIdx] = assets.loadTexture(texPath, VK_FORMAT_R8G8B8A8_UNORM);
            }
        }

        std::vector<entt::entity> entities;

        for (size_t shapeIdx = 0; shapeIdx < shapes.size(); ++shapeIdx) {
            const auto &shape = shapes[shapeIdx];

            struct MaterialBucket {
                int materialId = -1;
                std::unordered_map<Mesh::Vertex, uint32_t, VertexHasher> uniqueVertices;
                std::vector<Mesh::Vertex> vertices;
                std::vector<uint32_t> indices;
            };

            std::vector<MaterialBucket> buckets;
            std::unordered_map<int, size_t> materialToBucket;

            auto bucketForMaterial = [&](int materialId) -> MaterialBucket & {
                const auto it = materialToBucket.find(materialId);
                if (it != materialToBucket.end()) {
                    return buckets[it->second];
                }

                const size_t bucketIndex = buckets.size();
                materialToBucket.emplace(materialId, bucketIndex);
                buckets.push_back(MaterialBucket{materialId});
                return buckets.back();
            };

            size_t indexOffset = 0;
            for (size_t faceIdx = 0; faceIdx < shape.mesh.num_face_vertices.size(); ++faceIdx) {
                const size_t faceVertexCount = shape.mesh.num_face_vertices[faceIdx];
                if (faceVertexCount < 3 || indexOffset + faceVertexCount > shape.mesh.indices.size()) {
                    indexOffset += faceVertexCount;
                    continue;
                }

                const int materialId = faceIdx < shape.mesh.material_ids.size() ? shape.mesh.material_ids[faceIdx] : -1;
                auto &bucket = bucketForMaterial(materialId);

                std::vector<Mesh::Vertex> faceVertices;
                faceVertices.reserve(faceVertexCount);
                for (size_t vertexIdx = 0; vertexIdx < faceVertexCount; ++vertexIdx) {
                    faceVertices.push_back(makeVertex(attrib, shape.mesh.indices[indexOffset + vertexIdx]));
                }

                for (size_t tri = 1; tri + 1 < faceVertices.size(); ++tri) {
                    appendVertex(faceVertices[0], bucket.vertices, bucket.indices, bucket.uniqueVertices);
                    appendVertex(faceVertices[tri], bucket.vertices, bucket.indices, bucket.uniqueVertices);
                    appendVertex(faceVertices[tri + 1], bucket.vertices, bucket.indices, bucket.uniqueVertices);
                }

                indexOffset += faceVertexCount;
            }

            for (size_t bucketIdx = 0; bucketIdx < buckets.size(); ++bucketIdx) {
                auto &bucket = buckets[bucketIdx];
                if (bucket.vertices.empty()) {
                    continue;
                }

                finalizeVertexFrames(bucket.vertices, bucket.indices);

                const bool hasMultipleMaterials = buckets.size() > 1;
                std::string meshPath = path + "#shape" + std::to_string(shapeIdx);
                if (hasMultipleMaterials) {
                    meshPath += "_mat" + std::to_string(bucket.materialId);
                }

                AssetHandle meshHandle = assets.getOrCreateMesh(bucket.vertices, bucket.indices, meshPath);

                auto entity = registry.create();

                auto &sn = registry.emplace<SceneNodeComponent>(entity);
                sn.name = shape.name.empty() ? ("Shape_" + std::to_string(shapeIdx)) : shape.name;
                if (hasMultipleMaterials) {
                    sn.name += "_mat" + std::to_string(bucket.materialId);
                }
                sn.parent = parentEntity;

                if (parentEntity != entt::null && registry.valid(parentEntity)) {
                    if (auto *parentNode = registry.try_get<SceneNodeComponent>(parentEntity)) {
                        parentNode->children.push_back(entity);
                    }
                }

                auto &transform = registry.emplace<TransformComponent>(entity);
                transform.translation = glm::vec3(0.0f);
                transform.rotation = glm::vec3(0.0f);
                transform.scale = glm::vec3(1.0f);

                auto &modelComp = registry.emplace<ModelComponent>(entity);
                modelComp.meshHandle = meshHandle;

                auto &material = registry.emplace<MaterialComponent>(entity);
                if (bucket.materialId >= 0 && bucket.materialId < static_cast<int>(materials.size())) {
                    const auto &mat = materials[bucket.materialId];
                    material.baseColor = glm::vec4(mat.diffuse[0], mat.diffuse[1], mat.diffuse[2], mat.dissolve);
                    material.albedoTexture = albedoHandles[bucket.materialId];
                    material.normalMap = normalHandles[bucket.materialId];
                    material.metallicRoughnessMap = metallicHandles[bucket.materialId] != INVALID_ASSET_HANDLE
                                                        ? metallicHandles[bucket.materialId]
                                                        : roughnessHandles[bucket.materialId];
                    material.ambientOcclusion = ambientHandles[bucket.materialId];
                    material.alphaMasked = mat.dissolve < 1.0f;
                    material.transparent = mat.dissolve < 1.0f;
                } else {
                    material.baseColor = glm::vec4(1.0f);
                }

                entities.push_back(entity);
            }
        }

        AT_INFO("OBJAccessor: created {} entities from {}", entities.size(), path);
        return entities;
    }

    std::vector<std::byte> OBJAccessor::exportAsset(
        const std::vector<entt::entity> &entities,
        const entt::registry &registry) {
        std::vector<entt::entity> exportEntities;
        std::unordered_set<entt::id_type> visited;
        for (const entt::entity entity: entities) {
            appendEntityAndChildren(entity, registry, exportEntities, visited);
        }

        std::ostringstream obj;
        obj << std::fixed << std::setprecision(6);
        obj << "# Atlas OBJ export\n";

        uint32_t vertexOffset = 1;
        uint32_t texcoordOffset = 1;
        uint32_t normalOffset = 1;
        uint32_t exportedMeshes = 0;

        for (const entt::entity entity: exportEntities) {
            const auto *model = registry.try_get<ModelComponent>(entity);
            if (!model || model->meshHandle == INVALID_ASSET_HANDLE) {
                continue;
            }

            const auto mesh = assets.getMesh(model->meshHandle);
            if (!mesh || mesh->getVertices().empty()) {
                continue;
            }

            const auto &vertices = mesh->getVertices();
            const auto &indices = mesh->getIndices();
            const auto *transform = registry.try_get<TransformComponent>(entity);
            const auto *material = registry.try_get<MaterialComponent>(entity);
            const auto *node = registry.try_get<SceneNodeComponent>(entity);

            const std::string entityName = sanitizeObjName(
                node ? node->name : "",
                "Entity_" + std::to_string(entt::to_integral(entity))
            );

            glm::mat4 modelMatrix(1.0f);
            glm::mat3 normalMatrix(1.0f);
            bool reverseWinding = false;
            if (transform) {
                modelMatrix = transform->mat4();
                normalMatrix = transform->normalMatrix();
                reverseWinding = glm::determinant(glm::mat3(modelMatrix)) < 0.0f;
            }

            const glm::vec4 baseColor = material ? material->baseColor : glm::vec4(1.0f);

            obj << "\n";
            obj << "o " << entityName << "\n";
            if (material) {
                obj << "# baseColor " << baseColor.r << " " << baseColor.g << " " << baseColor.b << " " << baseColor.a << "\n";
                if (material->albedoTexture != INVALID_ASSET_HANDLE) {
                    obj << "# albedoTexture " << assets.getPath(material->albedoTexture) << "\n";
                }
                if (material->normalMap != INVALID_ASSET_HANDLE) {
                    obj << "# normalMap " << assets.getPath(material->normalMap) << "\n";
                }
            }

            for (const auto &vertex: vertices) {
                const glm::vec3 position = glm::vec3(modelMatrix * glm::vec4(vertex.position, 1.0f));
                const glm::vec3 color = glm::clamp(vertex.color * glm::vec3(baseColor), glm::vec3(0.0f), glm::vec3(1.0f));
                obj << "v "
                        << position.x << " " << position.y << " " << position.z << " "
                        << color.r << " " << color.g << " " << color.b << "\n";
            }

            for (const auto &vertex: vertices) {
                obj << "vt " << vertex.uv.x << " " << vertex.uv.y << "\n";
            }

            for (const auto &vertex: vertices) {
                glm::vec3 normal = normalMatrix * fallbackNormal(vertex.normal);
                normal = fallbackNormal(normal);
                obj << "vn " << normal.x << " " << normal.y << " " << normal.z << "\n";
            }

            auto writeFaceVertex = [&](uint32_t index) {
                const uint32_t objVertex = vertexOffset + index;
                const uint32_t objTexcoord = texcoordOffset + index;
                const uint32_t objNormal = normalOffset + index;
                obj << objVertex << "/" << objTexcoord << "/" << objNormal;
            };

            if (!indices.empty()) {
                for (size_t index = 0; index + 2 < indices.size(); index += 3) {
                    obj << "f ";
                    if (reverseWinding) {
                        writeFaceVertex(indices[index + 0]);
                        obj << " ";
                        writeFaceVertex(indices[index + 2]);
                        obj << " ";
                        writeFaceVertex(indices[index + 1]);
                    } else {
                        writeFaceVertex(indices[index + 0]);
                        obj << " ";
                        writeFaceVertex(indices[index + 1]);
                        obj << " ";
                        writeFaceVertex(indices[index + 2]);
                    }
                    obj << "\n";
                }
            } else {
                for (uint32_t index = 0; index + 2 < vertices.size(); index += 3) {
                    obj << "f ";
                    if (reverseWinding) {
                        writeFaceVertex(index + 0);
                        obj << " ";
                        writeFaceVertex(index + 2);
                        obj << " ";
                        writeFaceVertex(index + 1);
                    } else {
                        writeFaceVertex(index + 0);
                        obj << " ";
                        writeFaceVertex(index + 1);
                        obj << " ";
                        writeFaceVertex(index + 2);
                    }
                    obj << "\n";
                }
            }

            vertexOffset += static_cast<uint32_t>(vertices.size());
            texcoordOffset += static_cast<uint32_t>(vertices.size());
            normalOffset += static_cast<uint32_t>(vertices.size());
            ++exportedMeshes;
        }

        AT_INFO("OBJAccessor: exported {} meshes from {} requested entities", exportedMeshes, entities.size());
        return toBytes(obj.str());
    }
}
